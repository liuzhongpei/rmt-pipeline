/*
 * ndn.p4 - Named Data Networking forwarder with in-network cache
 *
 * NDN forwards by content name, not by host address:
 *   Interest (pkt_type=1): consumer asks for a name. Forwarded by
 *       FIB longest-prefix match on the name; the PIT remembers the
 *       ingress port ("breadcrumb") for the returning Data.
 *   Data (pkt_type=2): carries the content. It follows the PIT
 *       breadcrumbs back to the consumer and is cached on path in
 *       the Content Store (in-network cache).
 *
 * The two stateful stores are cache externs (dataplane-writable):
 *   cs  - Content Store : name -> content bytes
 *   pit - Pending Interest Table : name -> ingress port
 *
 * Packet walk (4 stages):
 *   stage 0  cs_t       Interest: cs_lookup   | Data: cs_insert
 *   stage 1  cs_hit_t   Interest w/ CS hit -> turn into Data reply
 *                       (egress = ingress, swap MACs); content is
 *                       injected from the cache at deparse time
 *   stage 2  pit_t      Interest miss: pit_insert (dup => aggregate)
 *                       Data: pit_consume -> reverse-path egress
 *   stage 3  fib_t      Interest miss: LPM(name) -> next hop
 *
 * Name = 128 bits, 4 x 32-bit hierarchical components; a FIB prefix
 * of /32 matches the first component (e.g. /video), /64 the first
 * two (e.g. /video/seg1).
 */

/* ------------------------- headers ------------------------- */
header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;            /* 0x8624 = NDN                  */
}

header ndn {
    pkt_type  : 8;             /* 1 = Interest, 2 = Data        */
    hop_limit : 8;
    name      : 128;           /* 4 x 32-bit name components    */
    nonce     : 32;
}

metadata meta {
    egress_port  : 16;
    drop         : 8;
    ingress_port : 16;         /* intrinsic (set at ingress)    */
    cs_hit       : 8;          /* set by cs_lookup on hit       */
    tmp_mac      : 48;         /* scratch for the MAC swap      */
}

/* ---------------- in-network cache externs ----------------- */
cache cs(64);                  /* Content Store                 */
cache pit(64);                 /* Pending Interest Table        */

/* ------------------------- parser -------------------------- */
parser start {
    extract(eth);
    select(eth.ethertype) {
        0x8624  : parse_ndn;
        default : accept;
    }
}

parser parse_ndn {
    extract(ndn);
    goto accept;
}

/* ------------------------- actions ------------------------- */
action cs_check() {            /* Interest: probe the cache     */
    cs_lookup(cs, ndn.name);
}

action cs_store() {            /* Data: cache content on path   */
    cs_insert(cs, ndn.name);
}

/* CS hit: answer the Interest locally with a Data reply.
 * The 3-mov MAC swap relies on the VLIW read-before-write commit:
 * word0 reads both MACs, word1 reads the committed scratch copy.  */
action cs_reply() {
    set(ndn.pkt_type, 2);
    set(meta.tmp_mac, eth.dmac);
    set(eth.dmac, eth.smac);
    set(eth.smac, meta.tmp_mac);
    set(meta.egress_port, meta.ingress_port);
}

action pit_add() {             /* breadcrumb: name -> in port   */
    pit_insert(pit, ndn.name);
}

action pit_serve() {           /* Data: back along breadcrumb   */
    pit_consume(pit, ndn.name);
}

action fwd(port) {             /* Interest: FIB next hop        */
    set(meta.egress_port, port);
    sub(ndn.hop_limit, 1);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action nop() {
}

/* ------------------------- tables -------------------------- */
table cs_t {
    key     { ndn.pkt_type : exact; }
    actions { cs_check; cs_store; nop; }
    default_action nop();
}

table cs_hit_t {
    key     { ndn.pkt_type : exact; meta.cs_hit : exact; }
    actions { cs_reply; nop; }
    default_action nop();
}

table pit_t {
    key     { ndn.pkt_type : exact; meta.cs_hit : exact; }
    actions { pit_add; pit_serve; nop; }
    default_action nop();
}

table fib_t {
    key {
        ndn.pkt_type : exact;
        meta.cs_hit  : exact;
        ndn.name     : lpm;
    }
    actions { fwd; drop_pkt; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(ndn)) {
        apply(cs_t);           /* stage 0 */
    }
    if (valid(ndn)) {
        apply(cs_hit_t);       /* stage 1 */
    }
    if (valid(ndn)) {
        apply(pit_t);          /* stage 2 */
    }
    if (valid(ndn)) {
        apply(fib_t);          /* stage 3 */
    }
}
