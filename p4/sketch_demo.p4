/*
 * sketch_demo.p4 - flow frequency estimation via Count-Min Sketch
 *
 * Demonstrates the sketch extern for tracking IPv4 flow frequencies.
 * The pipeline:
 *   stage 0  route        IPv4 LPM -> egress port
 *   stage 1  sketch_inc_t  update sketch for every IPv4 packet
 *   stage 2  sketch_get_t  query frequency estimate -> meta.freq_est
 *   stage 3  classify_t   heavy-hitter classification based on freq
 *   stage 4  mac_rewrite  egress port -> MAC rewrite
 *
 * P4 usage:
 *   - sketch flow_freq(4, 1024, ipv4.src, ipv4.dst);
 *     declares a CMS with d=4, w=1024 tracking (src,dst) pairs
 *   - sketch_inc(flow_freq);
 *     atomically increments 4 counters (one per hash row)
 *   - sketch_get(flow_freq, meta.freq_est);
 *     writes min(d counters) to meta.freq_est
 */

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

metadata meta {
    egress_port : 16;
    drop        : 8;
    freq_est    : 32;     /* sketch frequency estimate */
    heavy       : 8;      /* 1 if heavy hitter, 0 otherwise */
}

/* Count-Min Sketch: tracks (ipv4.src, ipv4.dst) pair frequency */
sketch flow_freq(4, 1024, ipv4.src, ipv4.dst);

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
    goto accept;
}

/* ------------------------- actions ------------------------- */
action forward(port) {
    set(meta.egress_port, port);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action count_flow() {
    sketch_inc(flow_freq);
}

action read_freq() {
    sketch_get(flow_freq, meta.freq_est);
}

action mark_heavy() {
    set(meta.heavy, 1);
    set(ipv4.tos, 46);       /* DSCP EF for heavy hitters */
}

action mark_normal() {
    set(meta.heavy, 0);
}

action set_macs(dmac, smac) {
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
table route {
    key     { ipv4.dst : lpm; }
    actions { forward; drop_pkt; }
    default_action drop_pkt();
}

table sketch_inc_t {
    key     { ipv4.proto : exact; }
    actions { count_flow; nop; }
    default_action count_flow();   /* count every IPv4 packet */
}

table sketch_get_t {
    key     { ipv4.proto : exact; }
    actions { read_freq; nop; }
    default_action read_freq();
}

table classify_t {
    key     { meta.freq_est : lpm; }
    actions { mark_heavy; mark_normal; }
    default_action mark_normal();
}

table mac_rewrite {
    key     { meta.egress_port : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(ipv4)) {
        apply(route);            /* stage 0: LPM route */
    }
    if (valid(ipv4)) {
        apply(sketch_inc_t);     /* stage 1: update sketch */
    }
    if (valid(ipv4)) {
        apply(sketch_get_t);     /* stage 2: query frequency */
    }
    if (valid(ipv4)) {
        apply(classify_t);       /* stage 3: classify heavy/normal */
    }
    apply(mac_rewrite);          /* stage 4: MAC rewrite */
}
