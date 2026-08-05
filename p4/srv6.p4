/*
 * srv6.p4 - SRv6 (Segment Routing over IPv6) endpoint forwarding on the
 *           RMT pipeline. 4 match-action stages:
 *
 *   stage 0  local_sid    "My SID" table: SRv6 End behavior
 *                         (SL-=1; dst = seg[SL]; hop_limit-=1)
 *   stage 1  route        IPv6 LPM routing -> egress port
 *   stage 2  acl          5-tuple classification (src, dst, proto,
 *                         sport, dport) -> permit / deny
 *   stage 3  mac_rewrite  egress port -> MAC rewrite
 */

/* ------------------------- headers ------------------------- */
header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;
}

header ipv6 {
    version     : 4;
    tclass      : 8;
    flowlabel   : 20;
    payload_len : 16;
    next_hdr    : 8;
    hop_limit   : 8;
    src         : 128;
    dst         : 128;
}

/* Segment Routing Header (RFC 8754) */
header srh {
    next_hdr      : 8;
    hdr_ext_len   : 8;
    routing_type  : 8;
    segments_left : 8;
    last_entry    : 8;
    flags         : 8;
    tag           : 16;
}

/* segment list: header stack of up to 8 SIDs */
header seg[8] {
    sid : 128;
}

header udp {
    sport : 16;
    dport : 16;
    len   : 16;
    csum  : 16;
}

metadata meta {
    egress_port : 16;
    drop        : 8;
}

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x86dd  : parse_ipv6;
        default : accept;
    }
}

parser parse_ipv6 {
    extract(ipv6);
    select(ipv6.next_hdr) {
        43      : parse_srh;    /* IPv6 Routing extension header */
        17      : parse_udp;
        default : accept;
    }
}

parser parse_srh {
    extract(srh);
    extract_array(seg, srh.last_entry + 1);
    select(srh.next_hdr) {
        17      : parse_udp;
        default : accept;
    }
}

parser parse_udp {
    extract(udp);
    goto accept;
}

/* ------------------------- actions ------------------------- */
/* SRv6 End behavior. The compiler schedules the two independent
 * subtractions into VLIW word 0 (two ALUs in parallel) and the
 * dependent indexed segment copy into word 1. */
action srv6_end() {
    sub(srh.segments_left, 1);
    sub(ipv6.hop_limit, 1);
    copy_indexed(ipv6.dst, seg.sid, srh.segments_left);
}

action forward(port) {
    set(meta.egress_port, port);
}

action set_macs(dmac, smac) {
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action permit() {
}

action deny() {
    set(meta.drop, 1);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
table local_sid {
    key     { ipv6.dst : lpm; }
    actions { srv6_end; nop; }
    default_action nop();
}

table route {
    key     { ipv6.dst : lpm; }
    actions { forward; drop_pkt; }
    default_action drop_pkt();
}

/* 5-tuple ACL: matched by the tree lookup engine */
table acl {
    key {
        ipv6.src      : lpm;
        ipv6.dst      : lpm;
        ipv6.next_hdr : exact;
        udp.sport     : exact;
        udp.dport     : exact;
    }
    actions { permit; deny; }
    default_action permit();
}

table mac_rewrite {
    key     { meta.egress_port : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(srh)) {
        apply(local_sid);       /* stage 0 */
    }
    apply(route);               /* stage 1 */
    if (valid(udp)) {
        apply(acl);             /* stage 2 */
    }
    apply(mac_rewrite);         /* stage 3 */
}
