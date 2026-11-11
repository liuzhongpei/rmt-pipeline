/*
 * ids.p4 - DNN-based anomaly traffic detection (in-network IDS)
 *          on the RMT pipeline
 *
 * A quantized MLP (dnn extern, integer-only Q16.16 forward pass) runs
 * per packet inside the pipeline and classifies TCP traffic from four
 * header features:
 *
 *     ipv4.len, ipv4.ttl, tcp.flags, tcp.window
 *
 *     class 0 = benign  (web data, legitimate handshakes)
 *     class 1 = anomaly (SYN flood, Xmas scan, NULL scan)
 *
 * The model is trained in float by the control plane (ids_demo.c),
 * quantized and installed with rmt_dnn_load(). No signature/entry
 * describes an attack: the verdict comes only from the network.
 *
 * 4 stages:
 *   stage 0  ids_t        TCP -> dnn_infer -> meta.class
 *   stage 1  verdict_t    class 1 -> drop (quarantine)
 *   stage 2  ttl_check    hop_limit=0 -> drop (RFC 1812)
 *   stage 3  route        IPv4 LPM -> egress port
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

header tcp {
    sport  : 16;
    dport  : 16;
    seq    : 32;
    ack    : 32;
    off    : 8;
    flags  : 8;
    window : 16;
    csum   : 16;
    urg    : 16;
}

metadata meta {
    egress_port : 16;
    drop        : 8;
    class       : 8;       /* dnn verdict: 0 benign, 1 anomaly */
}

/* --------------------- inference extern -------------------- */
/* MLP bound to its feature fields; topology + weights are
 * installed by the control plane after training. */
dnn ids(ipv4.len, ipv4.ttl, tcp.flags, tcp.window);

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x0800  : parse_ipv4;
        default : accept;
    }
}

parser parse_ipv4 {
    extract(ipv4);
    select(ipv4.proto) {
        6       : parse_tcp;
        default : accept;
    }
}

parser parse_tcp {
    extract(tcp);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action classify() {
    dnn_infer(ids, meta.class);
}

action quarantine() {
    set(meta.drop, 1);
}

action forward(port) {
    set(meta.egress_port, port);
    sub(ipv4.ttl, 1);
}

/* RFC 1812 §5.2.2: packets with TTL=0 must be discarded.
 * This table is applied before route to catch TTL=0 before
 * the decrement (which would underflow to 255).              */
action ttl_expired() {
    set(meta.drop, 1);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
/* stage 0: run the MLP on every TCP packet */
table ids_t {
    key     { ipv4.proto : exact; }
    actions { classify; nop; }
    default_action nop();
}

/* stage 1: act on the verdict */
table verdict_t {
    key     { meta.class : exact; }
    actions { quarantine; nop; }
    default_action nop();
}

/* stage 2: TTL expiry check (RFC 1812: drop TTL=0 before decrement) */
table ttl_check {
    key     { ipv4.ttl : exact; }
    actions { ttl_expired; nop; }
    default_action nop();
}

/* stage 3: normal L3 forwarding */
table route {
    key     { ipv4.dst : lpm; }
    actions { forward; drop_pkt; }
    default_action drop_pkt();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(tcp)) {
        apply(ids_t);          /* stage 0 */
    }
    if (valid(tcp)) {
        apply(verdict_t);      /* stage 1 */
    }
    if (valid(ipv4)) {
        apply(ttl_check);      /* stage 2: drop TTL=0 */
    }
    if (valid(ipv4)) {
        apply(route);          /* stage 3 */
    }
}
