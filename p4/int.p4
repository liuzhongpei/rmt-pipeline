/*
 * int.p4 - In-band Network Telemetry (INT-MD style) on the RMT pipeline
 *
 * Watched UDP flows (probe port 9000) collect one telemetry record per
 * hop inside the packet, between the UDP header and the payload:
 *
 *   eth | ipv4 | udp | int_shim | int_md[0] | int_md[1] | ... | payload
 *
 *   int_shim   4B   type=0x10, count = number of md records
 *   int_md    12B   switch_id, ingress port, egress port, timestamp
 *
 * Roles are decided purely by match keys -- the same program runs on
 * every switch:
 *   - INT source : watched flow (dst prefix + probe dport) and no INT
 *                  mark yet -> push shim + md[0], mark ipv4.tos=0x17
 *   - INT transit: INT mark present (tos == 0x17, count == 1, 2, or 3)
 *                  -> append md[count], count++
 * The parser only descends into the telemetry stack when the ipv4.tos
 * INT mark is set (a packet at the source hop has no stack yet).
 * meta.int_done keeps a switch from acting as source and transit for
 * the same packet. The collector (demo driver) decodes the stack.
 *
 * Supports up to 4 hops (1 source + 3 transit switches).
 * Note: int_transit_t entries must be configured per switch to activate
 * int_hop1/int_hop2/int_hop3 actions. Without entries, only source + 2
 * transit hops work (default demo configuration).
 *
 * 4 stages:
 *   stage 0  route          IPv4 LPM -> egress port
 *   stage 1  int_source_t   watched flow -> push shim + md[0]
 *   stage 2  int_transit_t  existing stack -> append md[count]
 *   stage 3  mac_rewrite    egress port -> MAC rewrite
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

header udp {
    sport : 16;
    dport : 16;
    len   : 16;
    csum  : 16;
}

/* INT shim: marks the packet as carrying telemetry */
header int_shim {
    type  : 8;             /* 0x10 = INT-MD                          */
    count : 8;             /* number of md records in the stack      */
    rsvd  : 16;
}

/* per-hop telemetry record (12 bytes) */
header int_md[4] {
    switch_id : 32;
    in_port   : 16;
    eg_port   : 16;
    ts        : 32;        /* ingress timestamp, ns (truncated)      */
}

metadata meta {
    egress_port  : 16;
    drop         : 8;
    ingress_port : 16;     /* intrinsic: set by pipeline at ingress  */
    timestamp    : 32;     /* intrinsic: arrival time, ns            */
    int_done     : 8;      /* this switch already added a record     */
}

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
        17      : parse_udp;
        default : accept;
    }
}

parser parse_udp {
    extract(udp);
    select(ipv4.tos) {
        0x17    : parse_int;    /* INT mark set by the source switch */
        default : accept;
    }
}

parser parse_int {
    extract(int_shim);
    extract_array(int_md, int_shim.count);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action forward(port) {
    set(meta.egress_port, port);
}

action drop_pkt() {
    set(meta.drop, 1);
}

/* INT source: push shim + first record, mark tos, grow lengths by 16 */
action int_source(sw) {
    add_header(int_shim);
    add_header(int_md[0]);
    set(ipv4.tos, 0x17);
    set(int_shim.type, 16);
    set(int_shim.count, 1);
    set(int_shim.rsvd, 0);
    set(int_md[0].switch_id, sw);
    set(int_md[0].in_port, meta.ingress_port);
    set(int_md[0].eg_port, meta.egress_port);
    set(int_md[0].ts, meta.timestamp);
    add(ipv4.len, 16);
    add(udp.len, 16);
    set(meta.int_done, 1);
}

/* INT transit: append record at the slot given by shim.count */
action int_hop1(sw) {
    add_header(int_md[1]);
    set(int_md[1].switch_id, sw);
    set(int_md[1].in_port, meta.ingress_port);
    set(int_md[1].eg_port, meta.egress_port);
    set(int_md[1].ts, meta.timestamp);
    set(int_shim.count, 2);
    add(ipv4.len, 12);
    add(udp.len, 12);
    set(meta.int_done, 1);
}

action int_hop2(sw) {
    add_header(int_md[2]);
    set(int_md[2].switch_id, sw);
    set(int_md[2].in_port, meta.ingress_port);
    set(int_md[2].eg_port, meta.egress_port);
    set(int_md[2].ts, meta.timestamp);
    set(int_shim.count, 3);
    add(ipv4.len, 12);
    add(udp.len, 12);
    set(meta.int_done, 1);
}

action int_hop3(sw) {
    add_header(int_md[3]);
    set(int_md[3].switch_id, sw);
    set(int_md[3].in_port, meta.ingress_port);
    set(int_md[3].eg_port, meta.egress_port);
    set(int_md[3].ts, meta.timestamp);
    set(int_shim.count, 4);
    add(ipv4.len, 12);
    add(udp.len, 12);
    set(meta.int_done, 1);
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

/* source role: watched flow (dst + probe port), no INT mark yet */
table int_source_t {
    key {
        ipv4.dst      : lpm;
        udp.dport     : exact;
        ipv4.tos      : exact;
    }
    actions { int_source; nop; }
    default_action nop();
}

/* transit role: INT mark present, not touched by this switch yet */
table int_transit_t {
    key {
        ipv4.tos : exact;
        int_shim.count : exact;
        meta.int_done : exact;
    }
    actions { int_hop1; int_hop2; int_hop3; nop; }
    default_action nop();
}

table mac_rewrite {
    key     { meta.egress_port : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    apply(route);                   /* stage 0 */
    if (valid(udp)) {
        apply(int_source_t);        /* stage 1 */
    }
    if (valid(udp)) {
        apply(int_transit_t);       /* stage 2 */
    }
    apply(mac_rewrite);             /* stage 3 */
}
