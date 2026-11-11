/*
 * test_parser.c - standalone tests for the programmable parser
 *
 * Verifies parser behavior with:
 *   - normal packets of various header combinations
 *   - short/truncated packets
 *   - unknown ethertypes
 *   - extract_array boundary conditions
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rmt.h"

static int pass = 0, fail = 0;

#define CHK(msg, cond) do { \
    if (cond) { printf("  [PASS] %s\n", msg); pass++; } \
    else      { printf("  [FAIL] %s\n", msg); fail++; } \
} while (0)

/* helper: run a compiled pipeline on a packet, return result code */
static int run_pkt(rmt_pipeline_t *p, const uint8_t *data, int len,
                   rmt_pkt_t *out)
{
    rmt_pkt_t in;
    memset(&in, 0, sizeof(in));
    if (len > 0)   /* NULL data is legal when len == 0; memcpy must not see it */
        memcpy(in.data, data, (size_t)(len < RMT_PKT_MAX ? len : RMT_PKT_MAX));
    in.len = len;
    in.in_port = 1;
    return rmt_process(p, &in, out);
}

/* build a minimal IPv4 packet */
static int build_ipv4(uint8_t *buf, int total_len)
{
    memset(buf, 0, total_len);
    /* eth */
    buf[12] = 0x08; buf[13] = 0x00;   /* ethertype IPv4 */
    /* ipv4 */
    buf[14] = 0x45;                     /* version=4, ihl=5 */
    buf[16] = (uint8_t)((total_len - 14) >> 8);
    buf[17] = (uint8_t)(total_len - 14);
    buf[22] = 10; buf[23] = 0; buf[24] = 0; buf[25] = 1;   /* src */
    buf[26] = 10; buf[27] = 0; buf[28] = 0; buf[29] = 2;   /* dst */
    return total_len;
}

/* ---- Test 1: parser handles valid IPv4 packet ---- */
static void test_parser_ipv4(void)
{
    printf("\n---- Parser Test 1: valid IPv4 packet ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int rc = p4c_compile_file(p, "p4/srv6.p4");
    CHK("compile srv6.p4", rc == 0);
    if (rc < 0) { rmt_destroy(p); return; }
    rmt_finalize(p);

    uint8_t pkt[128];
    int len = build_ipv4(pkt, 64);
    rmt_pkt_t out;
    rc = run_pkt(p, pkt, len, &out);
    CHK("process returns 0", rc == 0);

    rmt_destroy(p);
}

/* ---- Test 2: parser rejects short packet ---- */
static void test_parser_short(void)
{
    printf("\n---- Parser Test 2: truncated packet ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int rc = p4c_compile_file(p, "p4/srv6.p4");
    CHK("compile srv6.p4", rc == 0);
    if (rc < 0) { rmt_destroy(p); return; }
    rmt_finalize(p);

    /* only 10 bytes: too short for eth + ipv4 */
    uint8_t pkt[10] = {0};
    rmt_pkt_t out;
    rc = run_pkt(p, pkt, 10, &out);
    /* parser should fail gracefully (return -1 or drop) */
    CHK("short packet handled (rc<0 or dropped)", rc < 0 || out.dropped);

    rmt_destroy(p);
}

/* ---- Test 3: parser accepts unknown ethertype ---- */
static void test_parser_unknown_ether(void)
{
    printf("\n---- Parser Test 3: unknown ethertype ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int rc = p4c_compile_file(p, "p4/srv6.p4");
    CHK("compile srv6.p4", rc == 0);
    if (rc < 0) { rmt_destroy(p); return; }
    rmt_finalize(p);

    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    pkt[12] = 0x99; pkt[13] = 0x99;   /* unknown ethertype */
    rmt_pkt_t out;
    rc = run_pkt(p, pkt, 64, &out);
    CHK("unknown ethertype handled", rc == 0);

    rmt_destroy(p);
}

/* ---- Test 4: parser handles zero-length packet ---- */
static void test_parser_zero_len(void)
{
    printf("\n---- Parser Test 4: zero-length packet ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int rc = p4c_compile_file(p, "p4/srv6.p4");
    CHK("compile srv6.p4", rc == 0);
    if (rc < 0) { rmt_destroy(p); return; }
    rmt_finalize(p);

    rmt_pkt_t out;
    rc = run_pkt(p, NULL, 0, &out);
    CHK("zero-length handled", rc < 0 || out.dropped);

    rmt_destroy(p);
}

/* ---- Test 5: parser handles max-length packet ---- */
static void test_parser_max_len(void)
{
    printf("\n---- Parser Test 5: max-length packet ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int rc = p4c_compile_file(p, "p4/srv6.p4");
    CHK("compile srv6.p4", rc == 0);
    if (rc < 0) { rmt_destroy(p); return; }
    rmt_finalize(p);

    uint8_t pkt[RMT_PKT_MAX];
    int len = build_ipv4(pkt, RMT_PKT_MAX);
    rmt_pkt_t out;
    rc = run_pkt(p, pkt, len, &out);
    CHK("max-length packet handled", rc == 0);

    rmt_destroy(p);
}

/* ---- Test 6: non-byte-aligned fields round-trip bit-exactly ---- */
/* Regression for the extract fast path: a byte-wide field that starts
 * mid-byte (e.g. tclass:8 at bit 4, after version:4) must be read with
 * the bit-exact path, not a byte-aligned memcpy.  Build a synthetic
 * header 3+5+8+13+16+27+48+128 bits (31 bytes) and require parse ->
 * deparse to reproduce every random packet exactly. */
static void test_parser_bit_exact(void)
{
    printf("\n---- Parser Test 6: non-byte-aligned fields round-trip ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "h", false);
    static const int widths[] = { 3, 5, 8, 13, 16, 27, 48, 128 };
    int total = 0;
    for (int i = 0; i < 8; i++) {
        total += widths[i];
        if (rmt_field_add(p, h, "h.f", widths[i]) < 0)
            assert(0);
    }
    /* one-state parse graph: extract h, accept */
    p->start_state = 0;
    p->nstates = 1;
    parse_state_t *s = &p->states[0];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "start");
    s->sel_field = -1;
    s->def_next = PS_ACCEPT;
    s->nops = 1;
    s->ops[0].kind = POP_EXTRACT;
    s->ops[0].hdr = h;
    rmt_finalize(p);

    int nbytes = (total + 7) / 8;
    uint32_t seed = 12345;
    int bad = 0;
    uint8_t tls[RMT_PKT_MAX];
    for (int it = 0; it < 5000; it++) {
        uint8_t pkt[64], out[64];
        for (int i = 0; i < nbytes; i++) {
            seed = seed * 1103515245u + 12345u;
            pkt[i] = (uint8_t)(seed >> 16);
        }
        phv_t phv;
        memset(&phv, 0, sizeof(phv));
        phv.inject = tls;
        rmt_phv_reset(p, &phv);
        int hb = 0, n;
        memset(out, 0, sizeof(out));
        if (rmt_parse(p, pkt, nbytes, &phv, &hb) < 0 || hb != nbytes)
            bad++;
        else if ((n = rmt_deparse(p, &phv, out, (int)sizeof(out))) != nbytes)
            bad++;
        else if (memcmp(out, pkt, (size_t)nbytes) != 0)
            bad++;
    }
    CHK("5000 random non-aligned packets round-trip exactly", bad == 0);

    rmt_destroy(p);
}

int main(void)
{
    printf("====================================================\n");
    printf("Parser Unit Tests (standalone)\n");
    printf("====================================================\n");

    test_parser_ipv4();
    test_parser_short();
    test_parser_unknown_ether();
    test_parser_zero_len();
    test_parser_max_len();
    test_parser_bit_exact();

    printf("\n====================================================\n");
    if (fail == 0)
        printf("ALL TESTS PASSED (%d checks)\n", pass);
    else
        printf("TESTS: %d passed, %d failed\n", pass, fail);
    printf("====================================================\n");
    return fail ? 1 : 0;
}
