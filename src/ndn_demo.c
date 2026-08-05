/*
 * ndn_demo.c - Named Data Networking on the RMT pipeline,
 *              featuring the in-network cache (Content Store)
 *
 * Topology:
 *
 *   consumer1 --p1--+                +--p2-- producer
 *                   [ edge ] --p2==p1-- [ core ]
 *   consumer2 --p3--+
 *
 * Both routers run p4/ndn.p4 (CS -> PIT -> FIB, 4 stages) with two
 * cache externs each. The demo shows the NDN exchange:
 *
 *   1. consumer1 Interest /video/seg1  -> forwarded edge -> core
 *   2. producer Data                   -> back along PIT breadcrumbs,
 *                                         cached in every CS on path
 *   3. consumer2 same Interest         -> CS HIT at the edge: answered
 *                                         locally, producer never asked
 *   plus: FIB drop, unsolicited-Data drop, Interest aggregation.
 */
#include <stdio.h>
#include <string.h>
#include "rmt.h"

static int g_fails;

static void chk(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fails++;
}

/* ------------------- NDN packet construction ------------------- */
/* eth(14) | ndn: pkt_type(1) hop_limit(1) name(16) nonce(4) | payload */
#define NDN_OFF   14
#define NAME_OFF  16
#define PAYL_OFF  36

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void mk_ndn(rmt_pkt_t *pkt, int type, const uint8_t smac_tag,
                   uint32_t c0, uint32_t c1, uint32_t nonce,
                   const char *content)
{
    memset(pkt, 0, sizeof(*pkt));
    uint8_t *d = pkt->data;
    /* eth: to router MAC, from host tagged by smac_tag, NDN ethertype */
    memcpy(d, (uint8_t[]){ 0xaa, 0, 0, 0, 0xee, 0x01 }, 6);
    memcpy(d + 6, (uint8_t[]){ 0x02, 0, 0, 0, 0, smac_tag }, 6);
    d[12] = 0x86; d[13] = 0x24;
    d[NDN_OFF] = (uint8_t)type;        /* 1 Interest, 2 Data */
    d[NDN_OFF + 1] = 64;               /* hop limit          */
    put32(d + NAME_OFF, c0);           /* name components    */
    put32(d + NAME_OFF + 4, c1);
    put32(d + 32, nonce);
    pkt->len = PAYL_OFF;
    if (content) {
        size_t n = strlen(content);
        memcpy(d + PAYL_OFF, content, n);
        pkt->len += (int)n;
    }
}

static bool is_data(const rmt_pkt_t *p)   { return p->data[NDN_OFF] == 2; }
static bool payload_is(const rmt_pkt_t *p, const char *s)
{
    size_t n = strlen(s);
    return p->len == PAYL_OFF + (int)n &&
           !memcmp(p->data + PAYL_OFF, s, n);
}

/* name components ("vide", "seg1", ...) */
#define C_VIDEO 0x76696465u
#define C_SEG1  0x73656731u
#define C_SEG2  0x73656732u
#define C_SEG3  0x73656733u
#define C_MUSIC 0x6d757369u
#define C_ZZZ   0x7a7a7a7au

#define CONTENT1 "NDN DATA:/video/seg1 @producer"

int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && !strcmp(argv[1], "-v"));

    printf("=== NDN demo: name-based forwarding + in-network cache ===\n\n");
    rmt_pipeline_t *edge = rmt_create(), *core = rmt_create();
    if (p4c_compile_file(edge, "p4/ndn.p4") < 0 ||
        rmt_load_entries(edge, "entries/ndn_edge.txt") < 0 ||
        p4c_compile_file(core, "p4/ndn.p4") < 0 ||
        rmt_load_entries(core, "entries/ndn_core.txt") < 0)
        return 1;
    edge->verbose = core->verbose = verbose;

    rmt_pkt_t in, o1, o2, o3, o4;

    /* -- 1. consumer1 Interest /video/seg1: forwarded by name (FIB) -- */
    printf("---- 1. consumer1 Interest /video/seg1 ----\n");
    mk_ndn(&in, 1, 0xc1, C_VIDEO, C_SEG1, 0x1111, NULL);
    in.in_port = 1;
    rmt_process(edge, &in, &o1);              /* edge: CS miss, PIT add */
    chk("edge: CS miss -> FIB /video -> core (port 2)",
        !o1.dropped && o1.out_port == 2 && !is_data(&o1));
    chk("edge: hop_limit decremented", o1.data[NDN_OFF + 1] == 63);

    o1.in_port = 1;
    rmt_process(core, &o1, &o2);              /* core: same walk        */
    chk("core: CS miss -> FIB -> producer (port 2)",
        !o2.dropped && o2.out_port == 2);

    /* -- 2. producer answers with Data: PIT reverse path + caching -- */
    printf("\n---- 2. producer Data /video/seg1 (content %dB) ----\n",
           (int)strlen(CONTENT1));
    mk_ndn(&in, 2, 0xff, C_VIDEO, C_SEG1, 0x1111, CONTENT1);
    in.in_port = 2;
    rmt_process(core, &in, &o3);              /* core: cache + PIT      */
    chk("core: Data cached, PIT breadcrumb -> port 1",
        !o3.dropped && o3.out_port == 1 && payload_is(&o3, CONTENT1));

    o3.in_port = 2;
    rmt_process(edge, &o3, &o4);              /* edge: cache + PIT      */
    chk("edge: Data cached, delivered to consumer1 (port 1)",
        !o4.dropped && o4.out_port == 1 && payload_is(&o4, CONTENT1));

    /* -- 3. consumer2 asks the SAME name: served from the edge CS -- */
    printf("\n---- 3. consumer2 Interest /video/seg1 (cache hit) ----\n");
    mk_ndn(&in, 1, 0xc2, C_VIDEO, C_SEG1, 0x2222, NULL);
    in.in_port = 3;
    rmt_process(edge, &in, &o1);
    chk("edge: CS HIT -> Interest answered locally as Data",
        !o1.dropped && is_data(&o1));
    chk("edge: reply egress = ingress port 3", o1.out_port == 3);
    chk("edge: reply carries cached content", payload_is(&o1, CONTENT1));
    chk("edge: MACs swapped for the reply",
        o1.data[5] == 0xc2 && o1.data[11] == 0x01);

    /* -- 4. different segment: cache miss, forwarded upstream -- */
    printf("\n---- 4. consumer1 Interest /video/seg2 (other name) ----\n");
    mk_ndn(&in, 1, 0xc1, C_VIDEO, C_SEG2, 0x3333, NULL);
    in.in_port = 1;
    rmt_process(edge, &in, &o1);
    chk("edge: /video/seg2 not cached -> forwarded to core",
        !o1.dropped && !is_data(&o1) && o1.out_port == 2);

    /* -- 5. name outside the FIB: dropped by /0 catch-all -- */
    printf("\n---- 5. Interest /music/... (no FIB route) ----\n");
    mk_ndn(&in, 1, 0xc1, C_MUSIC, C_SEG1, 0x4444, NULL);
    in.in_port = 1;
    rmt_process(edge, &in, &o1);
    chk("edge: unroutable Interest dropped", o1.dropped);

    /* -- 6. unsolicited Data (no PIT entry): dropped -- */
    printf("\n---- 6. unsolicited Data /video/zzz ----\n");
    mk_ndn(&in, 2, 0xff, C_VIDEO, C_ZZZ, 0x5555, "bogus");
    in.in_port = 2;
    rmt_process(edge, &in, &o1);
    chk("edge: Data without PIT breadcrumb dropped", o1.dropped);

    /* -- 7. Interest aggregation: 2nd pending request suppressed -- */
    printf("\n---- 7. Interest aggregation /video/seg3 ----\n");
    mk_ndn(&in, 1, 0xc1, C_VIDEO, C_SEG3, 0x6666, NULL);
    in.in_port = 1;
    rmt_process(edge, &in, &o1);
    mk_ndn(&in, 1, 0xc2, C_VIDEO, C_SEG3, 0x7777, NULL);
    in.in_port = 3;
    rmt_process(edge, &in, &o2);
    chk("edge: 1st Interest forwarded, 2nd (same name pending) aggregated",
        !o1.dropped && o1.out_port == 2 && o2.dropped);

    printf("\ncache statistics:\n");
    printf(" edge:\n");
    for (int i = 0; i < edge->ncaches; i++)
        rmt_cache_print_stats(edge->caches[i]);
    printf(" core:\n");
    for (int i = 0; i < core->ncaches; i++)
        rmt_cache_print_stats(core->caches[i]);

    rmt_destroy(edge);
    rmt_destroy(core);

    printf("\n%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
