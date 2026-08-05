/*
 * int_demo.c - In-band Network Telemetry (INT) demo
 *
 * Topology (each hop is one RMT pipeline running p4/int.p4 with its
 * own control-plane entries):
 *
 *   host A ──p1──[ sw1 (source) ]──p2──p4──[ sw2 (transit) ]──p5──p6──[ sw3 (transit) ]──p7──> collector
 *
 * A watched UDP flow (dport 9000, dst 10.2.0.9) crosses 3 switches.
 * sw1 pushes the INT shim + its record, sw2/sw3 append theirs. The
 * collector at the end decodes the in-packet telemetry stack:
 * switch id, ingress/egress port and ingress timestamp per hop, plus
 * derived per-link latency.
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

static void hexdump(const char *title, const uint8_t *d, int n)
{
    printf("%s (%d bytes):\n", title, n);
    for (int i = 0; i < n; i += 16) {
        printf("  %04x: ", i);
        for (int j = i; j < i + 16 && j < n; j++)
            printf("%02x ", d[j]);
        printf("\n");
    }
}

static rmt_pipeline_t *mk_switch(const char *name, const char *entries)
{
    printf("[setup] %-4s : compile p4/int.p4 + load %s\n", name, entries);
    rmt_pipeline_t *sw = rmt_create();
    if (p4c_compile_file(sw, "p4/int.p4") < 0 ||
        rmt_load_entries(sw, entries) < 0) {
        fprintf(stderr, "FATAL: cannot set up %s\n", name);
        rmt_destroy(sw);
        return NULL;
    }
    return sw;
}

/* --------- the watched flow: UDP 10.1.0.5 -> 10.2.0.9:9000 --------- */
static void build_flow_pkt(rmt_pkt_t *in)
{
    static const uint8_t pkt[] = {
        /* eth: dst aa:..:01:01, src 02:00:00:00:00:0a, ipv4 */
        0xaa, 0x00, 0x00, 0x00, 0x01, 0x01,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x0a,
        0x08, 0x00,
        /* ipv4: len 36, ttl 64, proto 17, 10.1.0.5 -> 10.2.0.9 */
        0x45, 0x00, 0x00, 0x24, 0x00, 0x01, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00,
        0x0a, 0x01, 0x00, 0x05,
        0x0a, 0x02, 0x00, 0x09,
        /* udp: 3333 -> 9000 (INT probe port), len 16 */
        0x0d, 0x05, 0x23, 0x28, 0x00, 0x10, 0x00, 0x00,
        /* payload */
        'T', 'E', 'L', 'E', 'D', 'A', 'T', 'A',
    };
    memset(in, 0, sizeof(*in));
    memcpy(in->data, pkt, sizeof(pkt));
    in->len = (int)sizeof(pkt);
}

/* ------------------- collector: decode INT stack ------------------- */
#define SHIM_OFF 42                    /* eth 14 + ipv4 20 + udp 8     */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8  | p[3];
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void collector_report(const rmt_pkt_t *pkt)
{
    const uint8_t *shim = pkt->data + SHIM_OFF;
    int count = shim[1];

    printf("\n=============== INT collector report ===============\n");
    printf("  shim: type=0x%02x, records=%d\n", shim[0], count);
    printf("  %-5s %-10s %-8s %-8s %-12s %s\n",
           "hop", "switch_id", "in_port", "eg_port", "ts(ns)", "link latency");
    uint32_t prev_ts = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t *md = shim + 4 + i * 12;
        uint32_t sw = rd32(md), ts = rd32(md + 8);
        uint16_t ip = rd16(md + 4), ep = rd16(md + 6);
        if (i == 0)
            printf("  %-5d %-10u %-8u %-8u %-12u -\n", i, sw, ip, ep, ts);
        else
            printf("  %-5d %-10u %-8u %-8u %-12u %u ns\n", i, sw, ip, ep,
                   ts, ts - prev_ts);
        prev_ts = ts;
    }
    printf("=====================================================\n");
}

int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && !strcmp(argv[1], "-v"));

    printf("=== INT (In-band Network Telemetry) demo: 3-switch path ===\n\n");
    rmt_pipeline_t *sw1 = mk_switch("sw1", "entries/int_sw1.txt");
    rmt_pipeline_t *sw2 = mk_switch("sw2", "entries/int_sw2.txt");
    rmt_pipeline_t *sw3 = mk_switch("sw3", "entries/int_sw3.txt");
    if (!sw1 || !sw2 || !sw3)
        return 1;
    sw1->verbose = sw2->verbose = sw3->verbose = verbose;

    rmt_pkt_t in, h1, h2, h3;
    build_flow_pkt(&in);
    in.in_port = 1;
    printf("\n[flow] UDP 10.1.0.5:3333 -> 10.2.0.9:9000, payload 8B, %dB total\n",
           in.len);

    /* hop 1: sw1 = INT source */
    printf("\n---- hop 1: sw1 (INT source) ----\n");
    if (rmt_process(sw1, &in, &h1) < 0 || h1.dropped)
        { chk("sw1 forwarded packet", false); return 1; }
    printf("  out: %dB -> port %d  (pushed shim + md[0], +16B)\n",
           h1.len, h1.out_port);

    /* hop 2: sw2 = transit (packet enters on port 4) */
    printf("\n---- hop 2: sw2 (INT transit) ----\n");
    h1.in_port = 4;
    if (rmt_process(sw2, &h1, &h2) < 0 || h2.dropped)
        { chk("sw2 forwarded packet", false); return 1; }
    printf("  out: %dB -> port %d  (appended md[1], +12B)\n",
           h2.len, h2.out_port);

    /* hop 3: sw3 = transit (packet enters on port 6) */
    printf("\n---- hop 3: sw3 (INT transit) ----\n");
    h2.in_port = 6;
    if (rmt_process(sw3, &h2, &h3) < 0 || h3.dropped)
        { chk("sw3 forwarded packet", false); return 1; }
    printf("  out: %dB -> port %d  (appended md[2], +12B)\n",
           h3.len, h3.out_port);

    hexdump("\nfinal packet at collector", h3.data, h3.len);
    collector_report(&h3);

    /* ------------------------- checks ------------------------- */
    printf("\nresults:\n");
    const uint8_t *shim = h3.data + SHIM_OFF;
    chk("packet grew 50 -> 90 bytes (shim 4 + 3 x md 12)", h3.len == 90);
    chk("INT shim type 0x10, 3 records", shim[0] == 0x10 && shim[1] == 3);
    chk("ipv4.len updated 36 -> 76", rd16(h3.data + 16) == 76);
    chk("udp.len updated 16 -> 56", rd16(h3.data + 38) == 56);
    chk("hop path switch_id = 1, 2, 3",
        rd32(shim + 4) == 1 && rd32(shim + 16) == 2 && rd32(shim + 28) == 3);
    chk("ingress ports recorded = 1, 4, 6",
        rd16(shim + 8) == 1 && rd16(shim + 20) == 4 && rd16(shim + 32) == 6);
    chk("egress ports recorded = 2, 5, 7",
        rd16(shim + 10) == 2 && rd16(shim + 22) == 5 && rd16(shim + 34) == 7);
    chk("timestamps monotonic per hop",
        rd32(shim + 12) != 0 &&
        (uint32_t)(rd32(shim + 24) - rd32(shim + 12)) < 0x80000000u &&
        (uint32_t)(rd32(shim + 36) - rd32(shim + 24)) < 0x80000000u);
    chk("payload intact after telemetry stack",
        !memcmp(h3.data + h3.len - 8, "TELEDATA", 8));
    chk("MACs rewritten by last hop", h3.data[4] == 0x03 && h3.data[5] == 0x07);

    /* a non-watched flow (dport != 9000) must pass through untouched */
    rmt_pkt_t plain, po;
    build_flow_pkt(&plain);
    plain.data[36] = 0x23; plain.data[37] = 0x29;   /* dport 9001 */
    plain.in_port = 1;
    rmt_process(sw1, &plain, &po);
    chk("non-watched flow untouched (no INT push)",
        !po.dropped && po.len == 50 && rd16(po.data + 38) == 16);

    rmt_destroy(sw1);
    rmt_destroy(sw2);
    rmt_destroy(sw3);

    printf("\n%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
