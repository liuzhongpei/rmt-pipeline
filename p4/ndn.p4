/*
 * ndn.p4 - Named Data Networking forwarder (enhanced)
 *
 * Full NDN forwarder with:
 *   - Hop limit exhaustion check (TTL=0 protection)
 *   - Interest lifetime + PIT timeout via cache aging
 *   - Nonce-based loop detection
 *   - NACK generation on FIB miss
 *   - CS freshness (stale bit based on cache aging)
 *   - 128-bit name (4 x 32-bit components)
 *
 * Packet types:
 *   1 = Interest (request for content by name)
 *   2 = Data     (content with name, follows PIT breadcrumbs)
 *   3 = NACK     (negative acknowledgement: name + error code)
 *
 * Pipeline (8 stages):
 *   stage 0  hop_check_t     drop if hop_limit == 0 (TTL protection)
 *   stage 1  nonce_probe_t   probe nonce_seen cache -> meta.used
 *   stage 2  nonce_check_t   meta.used=1 -> drop; else record nonce
 *   stage 3  cs_t            Interest: cs_lookup | Data: cs_insert
 *   stage 4  cs_hit_t        Interest w/ CS hit -> turn into Data reply
 *   stage 5  pit_t           Interest miss: pit_insert
 *                            Data: pit_consume -> reverse-path egress
 *   stage 6  fib_t           Interest: LPM(name) -> next hop
 *                            FIB miss -> generate NACK
 *   stage 7  mac_rewrite     egress port -> MAC rewrite
 *
 * Name = 128 bits, 4 x 32-bit hierarchical components. A FIB prefix
 * of /32 matches the first component (e.g. /video), /64 the first two.
 */

/* ------------------------- headers ------------------------- */
header eth {
    dmac      : 48;
    smac      : 48;
    ethertype : 16;            /* 0x8624 = NDN                  */
}

header ndn {
    pkt_type  : 8;             /* 1=Interest, 2=Data, 3=NACK    */
    hop_limit : 8;             /* TTL, decremented at each hop  */
    lifetime  : 16;            /* Interest lifetime (seconds x10) */
    name      : 128;           /* 4 x 32-bit name components    */
    nonce     : 32;            /* random nonce (loop detection) */
    error_code: 8;             /* NACK reason (0=ok, 1=no route, 2=timeout) */
    reserved  : 8;             /* padding / future use          */
}

metadata meta {
    egress_port  : 16;
    drop         : 8;
    ingress_port : 16;         /* intrinsic (set at ingress)    */
    cs_hit       : 8;          /* set by cs_lookup on hit       */
    stale        : 8;          /* set if CS entry is stale      */
    used         : 8;          /* util_probe result (nonce seen)*/
    tmp_mac      : 48;         /* scratch for the MAC swap      */
    fib_hit      : 8;          /* set by fib_t on hit           */
    pit_ports    : 16;         /* multi-port PIT bitmap (8 ports) */
    nack_gen     : 8;          /* set by make_nack: skip mac_rewrite */
}

/* ---------------- in-network cache externs ----------------- */
cache cs(64);                  /* Content Store                 */
cache pit(64);                 /* Pending Interest Table        */
cache nonce_seen(256);         /* Nonce loop detection          */

/* Sketch: track per-name Interest frequency for popularity stats.
 * Used by cache replacement policies (popularity-aware caching). */
sketch name_freq(4, 256, ndn.name);

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

/* ---- stage 0: hop limit check ---- */
action hop_expired() {
    set(meta.drop, 1);
}

/* ---- stage 1: nonce loop detection ---- */
action nonce_probe() {
    util_probe(nonce_seen, ndn.nonce);     /* meta.used=1 if seen */
}

action nonce_drop() {
    set(meta.drop, 1);                   /* loop detected */
}

action nonce_record() {
    util_mark(nonce_seen, ndn.nonce);    /* record this nonce */
}

/* ---- stage 2: CS lookup / insert + name tracking ---- */
action cs_check() {
    cs_lookup(cs, ndn.name);
    sketch_inc(name_freq);            /* track Interest popularity */
}

action cs_store() {
    cs_insert(cs, ndn.name);
}

/* track name popularity in the sketch */
action track_name() {
    sketch_inc(name_freq);
}

/* ---- stage 3: CS hit reply ---- */
/* CS hit: answer the Interest locally with a Data reply.
 * The 3-mov MAC swap relies on the VLIW read-before-write commit.  */
action cs_reply() {
    set(ndn.pkt_type, 2);
    set(meta.tmp_mac, eth.dmac);
    set(eth.dmac, eth.smac);
    set(eth.smac, meta.tmp_mac);
    set(meta.egress_port, meta.ingress_port);
    set(meta.stale, 0);
}

/* ---- stage 4: PIT operations ---- */
action pit_add() {
    /* breadcrumb: name -> in port.
     * Nonce was already recorded by nonce_check_t at stage 2. */
    pit_insert(pit, ndn.name);
}

action pit_serve() {
    /* Data: follow the breadcrumb back */
    pit_consume(pit, ndn.name);
}

/* ---- stage 5: FIB + NACK ---- */
action fwd(port) {
    set(meta.egress_port, port);
    set(meta.fib_hit, 1);
    sub(ndn.hop_limit, 1);
}

action drop_pkt() {
    set(meta.drop, 1);
}

action make_nack(errcode) {
    /* FIB miss for Interest: generate NACK back to requester */
    set(ndn.pkt_type, 3);
    set(ndn.error_code, errcode);
    set(ndn.hop_limit, 64);
    set(meta.tmp_mac, eth.dmac);
    set(eth.dmac, eth.smac);
    set(eth.smac, meta.tmp_mac);
    set(meta.egress_port, meta.ingress_port);
    set(meta.nack_gen, 1);         /* skip mac_rewrite */
}

/* ---- stage 6: NACK handling ---- */
action nack_nop() {
}

/* ---- stage 7: MAC rewrite ---- */
action set_macs(dmac, smac) {
    set(eth.dmac, dmac);
    set(eth.smac, smac);
}

action nop() {
}

/* ------------------------- tables -------------------------- */

/* stage 0: drop packets with exhausted hop limit */
table hop_check_t {
    key     { ndn.hop_limit : exact; }
    actions { hop_expired; nop; }
    default_action nop();
}

/* stage 1a: probe the nonce cache (sets meta.used=1 if seen before).
 * Only probe for Interest packets; Data/NACK don't need loop detect. */
table nonce_probe_t {
    key     { ndn.pkt_type : exact; }
    actions { nonce_probe; nop; }
    default_action nop();    /* only Interest triggers probe via entry */
}

/* stage 1b: if nonce was seen (meta.used=1), drop; else record for Interest only.
 * Data/NACK packets don't need nonce tracking (default: nop). */
table nonce_check_t {
    key     { ndn.pkt_type : exact; meta.used : exact; }
    actions { nonce_drop; nonce_record; nop; }
    default_action nop();   /* Data/NACK: pass through; Interest: see entries */
}

/* stage 2: CS operations by packet type */
table cs_t {
    key     { ndn.pkt_type : exact; }
    actions { cs_check; cs_store; nop; }
    default_action nop();
}

/* stage 3: CS hit reply */
table cs_hit_t {
    key     { ndn.pkt_type : exact; meta.cs_hit : exact; }
    actions { cs_reply; nop; }
    default_action nop();
}

/* stage 4: PIT operations */
table pit_t {
    key     { ndn.pkt_type : exact; meta.cs_hit : exact; }
    actions { pit_add; pit_serve; nop; }
    default_action nop();
}

/* stage 5: FIB lookup; miss generates NACK */
table fib_t {
    key {
        ndn.pkt_type : exact;
        meta.cs_hit  : exact;
        ndn.name     : lpm;
    }
    actions { fwd; make_nack; drop_pkt; nop; }
    default_action nop();    /* Data pkts rely on pit_serve's port */
}

/* stage 6: MAC rewrite (skip for CS replies and NACKs which have their own swap) */
table mac_rewrite {
    key     { meta.egress_port : exact; meta.cs_hit : exact; meta.nack_gen : exact; }
    actions { set_macs; nop; }
    default_action nop();
}

/* --------------------- control flow ------------------------ */
control ingress {
    if (valid(ndn)) {
        apply(hop_check_t);       /* stage 0: TTL protection */
    }
    if (valid(ndn)) {
        apply(nonce_probe_t);     /* stage 1a: probe nonce */
    }
    if (valid(ndn)) {
        apply(nonce_check_t);     /* stage 1b: drop if seen, record if new */
    }
    if (valid(ndn)) {
        apply(cs_t);              /* stage 2: CS lookup/insert */
    }
    if (valid(ndn)) {
        apply(cs_hit_t);          /* stage 3: CS hit reply */
    }
    if (valid(ndn)) {
        apply(pit_t);             /* stage 4: PIT add/serve */
    }
    if (valid(ndn)) {
        apply(fib_t);             /* stage 5: FIB + NACK gen */
    }
    apply(mac_rewrite);           /* stage 6: MAC rewrite */
}
