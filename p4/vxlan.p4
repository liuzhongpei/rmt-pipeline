/*
 * vxlan.p4 - VXLAN gateway (VTEP): decapsulation and encapsulation
 *
 *   stage 0  vxlan_term  gate valid(vxlan): VNI -> decap (strip outer
 *                        eth/ipv4/udp/vxlan; inner frame is forwarded)
 *   stage 1  tenant      gate valid(ipv4):  tenant IPv4 LPM -> encap
 *                        (copy parsed eth/ipv4 into inner slots, build
 *                        outer IPv4/UDP/VXLAN around them). Skipped
 *                        for decapped packets (outer ipv4 invalidated).
 *   stage 2  l2          gate valid(ieth):  inner MAC -> egress port
 *                        (overlay bridging after decap)
 *   stage 3  underlay    gate valid(ipv4):  underlay route for the
 *                        encapsulated packet -> port + outer MACs
 *
 * The encap action is a good showcase of the 16-ALU VLIW engine: the
 * 15 inner-header field copies plus the first batch of outer-header
 * rewrites execute in one parallel word (read-before-write semantics
 * let the copies observe the pre-rewrite values).
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

header udp {
    sport : 16;
    dport : 16;
    len   : 16;
    csum  : 16;
}

header vxlan {
    flags : 8;
    rsvd  : 24;
    vni   : 24;
    rsvd2 : 8;
}

/* inner (tenant) headers */
header ieth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;
}

header iipv4 {
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
    select(udp.dport) {
        4789    : parse_vxlan;
        default : accept;
    }
}

parser parse_vxlan {
    extract(vxlan);
    goto parse_ieth;
}

parser parse_ieth {
    extract(ieth);
    select(ieth.ethertype) {
        0x0800  : parse_iipv4;
        default : accept;
    }
}

parser parse_iipv4 {
    extract(iipv4);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action vxlan_decap() {
    remove_header(eth);
    remove_header(ipv4);
    remove_header(udp);
    remove_header(vxlan);
}

/* Build outer IPv4/UDP/VXLAN around the parsed tenant frame.
 * Copies must appear before the outer rewrites so that the scheduler
 * lets them read the original values (same-word WAR is safe). */
action vxlan_encap(vni, sip, dip) {
    add_header(udp);
    add_header(vxlan);
    add_header(ieth);
    add_header(iipv4);

    /* tenant eth/ipv4 -> inner slots (15 parallel field moves) */
    set(ieth.dmac,      eth.dmac);
    set(ieth.smac,      eth.smac);
    set(ieth.ethertype, eth.ethertype);
    set(iipv4.version,  ipv4.version);
    set(iipv4.ihl,      ipv4.ihl);
    set(iipv4.tos,      ipv4.tos);
    set(iipv4.len,      ipv4.len);
    set(iipv4.id,       ipv4.id);
    set(iipv4.flags,    ipv4.flags);
    set(iipv4.frag,     ipv4.frag);
    set(iipv4.ttl,      ipv4.ttl);
    set(iipv4.proto,    ipv4.proto);
    set(iipv4.csum,     ipv4.csum);
    set(iipv4.src,      ipv4.src);
    set(iipv4.dst,      ipv4.dst);

    /* outer UDP length = tenant len + 14(ieth) + 8(vxlan) + 8(udp) */
    set(udp.len, ipv4.len);
    add(udp.len, 30);

    /* outer IPv4 (reuses the parsed slot): len = tenant len + 50 */
    add(ipv4.len, 50);
    set(ipv4.src,   sip);
    set(ipv4.dst,   dip);
    set(ipv4.ttl,   64);
    set(ipv4.proto, 17);
    set(ipv4.csum,  0);

    set(udp.sport, 49330);
    set(udp.dport, 4789);
    set(udp.csum,  0);

    set(vxlan.flags, 8);
    set(vxlan.rsvd,  0);
    set(vxlan.vni,   vni);
    set(vxlan.rsvd2, 0);
}

action set_port(port) {
    set(meta.egress_port, port);
}

action route_underlay(port, dmac, smac) {
    set(meta.egress_port, port);
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
table vxlan_term {
    key     { vxlan.vni : exact; }
    actions { vxlan_decap; drop_pkt; nop; }
    default_action nop();
}

table tenant {
    key     { ipv4.dst : lpm; }
    actions { vxlan_encap; nop; }
    default_action nop();
}

table l2 {
    key     { ieth.dmac : exact; }
    actions { set_port; nop; }
    default_action nop();
}

table underlay {
    key     { ipv4.dst : lpm; }
    actions { route_underlay; drop_pkt; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(vxlan)) {
        apply(vxlan_term);      /* stage 0 */
    }
    if (valid(ipv4)) {
        apply(tenant);          /* stage 1 */
    }
    if (valid(ieth)) {
        apply(l2);              /* stage 2 */
    }
    if (valid(ipv4)) {
        apply(underlay);        /* stage 3 */
    }
}
