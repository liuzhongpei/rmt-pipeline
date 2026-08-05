/*
 * telescope.p4 - MORP4-style dynamic network telescope
 *                on the RMT pipeline
 *
 * Deployed at the single ingress/egress point of an organization's
 * network (port 1 = uplink/Internet, port 2 = internal LAN). The
 * switch infers -- in the dataplane, per packet -- which addresses
 * of the monitored space are in use, and captures only inbound
 * traffic towards *unused* addresses (Internet Background Radiation):
 *
 *   outbound pkt : its source address is alive -> util_mark
 *   inbound  pkt : probe the destination       -> util_probe
 *       used   -> forward normally (confidential, NEVER captured)
 *       unused -> telescope: redirect to the collector (port 9)
 *
 * Utilization inference and the capture decision run in the SAME
 * pipeline, so an address activating with its first outbound packet
 * is visible to every later inbound lookup -- the MORP4 correctness
 * property. Aging (epoch TTL) and the monitored prefixes (space
 * granularity) are control-plane configurable. Dual-stack: the same
 * design gives an IPv6 telescope, impossible with static darknets.
 *
 * 6 stages:
 *   stage 0  dir_t      ingress port -> direction (out/in)
 *   stage 1  mark4_t    outbound IPv4: mark src used
 *   stage 2  probe4_t   inbound IPv4 in monitored space: probe dst
 *   stage 3  mark6_t    outbound IPv6: mark src used
 *   stage 4  probe6_t   inbound IPv6 in monitored space: probe dst
 *   stage 5  decide_t   (dir, scoped, used) -> capture / forward
 */

/* ------------------------- headers ------------------------- */
header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;
}

header ipv4 {
    version : 4;
    ihl     : 4;
    tos     : 8;
    len     : 16;
    id      : 16;
    flags   : 3;
    frag    : 13;
    ttl     : 8;
    proto   : 8;
    csum    : 16;
    src     : 32;
    dst     : 32;
}

header ipv6 {
    version     : 4;
    tclass      : 8;
    flow        : 20;
    payload_len : 16;
    next_hdr    : 8;
    hop_limit   : 8;
    src         : 128;
    dst         : 128;
}

metadata meta {
    ingress_port : 16;     /* intrinsic: set by pipeline at ingress  */
    egress_port  : 16;
    drop         : 8;
    dir          : 8;      /* 0 = inbound (from uplink), 1 = outbound */
    scoped       : 8;      /* dst inside the monitored space          */
    used         : 8;      /* util_probe verdict: dst address in use  */
    captured     : 8;      /* packet goes to the telescope collector  */
}

/* ---------------- utilization tables (externs) -------------- */
/* Dataplane-writable per-address state with epoch aging; the
 * control plane configures TTL and advances epochs (time
 * granularity of the telescope). One table per family. */
cache util4(256);
cache util6(256);

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x0800  : parse_ipv4;
        0x86dd  : parse_ipv6;
        default : accept;
    }
}

parser parse_ipv4 {
    extract(ipv4);
    goto accept;
}

parser parse_ipv6 {
    extract(ipv6);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action set_dir(d) {
    set(meta.dir, d);
}

/* outbound: the source address is alive at the current epoch */
action mark4() {
    util_mark(util4, ipv4.src);
}

action mark6() {
    util_mark(util6, ipv6.src);
}

/* inbound within monitored space: is the destination in use? */
action probe4() {
    set(meta.scoped, 1);
    util_probe(util4, ipv4.dst);
}

action probe6() {
    set(meta.scoped, 1);
    util_probe(util6, ipv6.dst);
}

/* unsolicited traffic -> telescope collector */
action capture(port) {
    set(meta.egress_port, port);
    set(meta.captured, 1);
}

action fwd(port) {
    set(meta.egress_port, port);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
/* stage 0: direction from the ingress link */
table dir_t {
    key     { meta.ingress_port : exact; }
    actions { set_dir; nop; }
    default_action nop();
}

/* stages 1-4: utilization inference + probe, per family */
table mark4_t {
    key     { meta.dir : exact; }
    actions { mark4; nop; }
    default_action nop();
}

table probe4_t {
    key     { meta.dir : exact; ipv4.dst : lpm; }
    actions { probe4; nop; }
    default_action nop();
}

table mark6_t {
    key     { meta.dir : exact; }
    actions { mark6; nop; }
    default_action nop();
}

table probe6_t {
    key     { meta.dir : exact; ipv6.dst : lpm; }
    actions { probe6; nop; }
    default_action nop();
}

/* stage 5: capture decision, coupled with the inference above */
table decide_t {
    key     { meta.dir : exact; meta.scoped : exact; meta.used : exact; }
    actions { capture; fwd; drop_pkt; }
    default_action drop_pkt();
}

/* --------------------- control flow ------------------------ */
control ingress {
    apply(dir_t);              /* stage 0 */
    if (valid(ipv4)) {
        apply(mark4_t);        /* stage 1 */
        apply(probe4_t);       /* stage 2 */
    }
    if (valid(ipv6)) {
        apply(mark6_t);        /* stage 3 */
        apply(probe6_t);       /* stage 4 */
    }
    apply(decide_t);           /* stage 5 */
}
