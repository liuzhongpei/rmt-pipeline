/*
 * mpls.p4 - multi-layer (nested) MPLS label stack forwarding
 *
 * The parser walks the label stack instance by instance (up to 4
 * labels) following the bottom-of-stack (s) bit. Each pipeline stage
 * owns one nesting level of the stack, so a tunnel-terminating LSR can
 * pop several outer labels and swap the inner one in a single pass:
 *
 *   stage 0  mpls0        top label     : pop / swap+ttl-1
 *   stage 1  mpls1        2nd label     : pop / swap+ttl-1
 *   stage 2  mpls2        3rd label     : swap+ttl-1 -> egress port
 *   stage 3  mac_rewrite  egress port   : MAC rewrite
 *
 * Example flows (see entries/mpls_entries.txt):
 *   A: [100|200|300] -> pop 100, pop 200, swap 300->399  (nested x2)
 *   B: [500|600]     -> swap 500->501, transit (inner 600 untouched)
 */

header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;
}

/* MPLS label stack, up to 4 nested labels */
header mpls[4] {
    label : 20;
    tc    : 3;
    s     : 1;
    ttl   : 8;
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

metadata meta {
    egress_port : 16;
    drop        : 8;
}

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x8847  : parse_mpls0;
        0x0800  : parse_ipv4;
        default : accept;
    }
}

parser parse_mpls0 {
    extract(mpls[0]);
    select(mpls[0].s) {
        0       : parse_mpls1;
        default : parse_ipv4;
    }
}

parser parse_mpls1 {
    extract(mpls[1]);
    select(mpls[1].s) {
        0       : parse_mpls2;
        default : parse_ipv4;
    }
}

parser parse_mpls2 {
    extract(mpls[2]);
    select(mpls[2].s) {
        0       : parse_mpls3;
        default : parse_ipv4;
    }
}

parser parse_mpls3 {
    extract(mpls[3]);
    goto parse_ipv4;
}

parser parse_ipv4 {
    extract(ipv4);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action pop0() {
    remove_header(mpls[0]);
}

action pop1() {
    remove_header(mpls[1]);
}

action swap0(lbl, port) {
    set(mpls[0].label, lbl);
    sub(mpls[0].ttl, 1);
    set(meta.egress_port, port);
}

action swap2(lbl, port) {
    set(mpls[2].label, lbl);
    sub(mpls[2].ttl, 1);
    set(meta.egress_port, port);
}

action set_macs(dmac, smac) {
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
table mpls0 {
    key     { mpls[0].label : exact; }
    actions { pop0; swap0; drop_pkt; nop; }
    default_action nop();
}

table mpls1 {
    key     { mpls[1].label : exact; }
    actions { pop1; nop; }
    default_action nop();
}

table mpls2 {
    key     { mpls[2].label : exact; }
    actions { swap2; nop; }
    default_action nop();
}

table mac_rewrite {
    key     { meta.egress_port : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(mpls[0])) {
        apply(mpls0);           /* stage 0: outer label   */
    }
    if (valid(mpls[1])) {
        apply(mpls1);           /* stage 1: nested label  */
    }
    if (valid(mpls[2])) {
        apply(mpls2);           /* stage 2: nested label  */
    }
    apply(mac_rewrite);         /* stage 3                */
}
