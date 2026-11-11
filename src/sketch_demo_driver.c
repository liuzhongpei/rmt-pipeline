/*
 * sketch_demo_driver.c - driver to demo sketch statistics
 *
 * Sends IPv4 packets through the sketch pipeline and shows the
 * frequency estimates for different flows.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rmt.h"

/* build an IPv4 packet with given src/dst. Returns packet length. */
static int build_ipv4(uint8_t *pkt, int pkt_len,
                      uint8_t s1, uint8_t s2, uint8_t s3, uint8_t s4,
                      uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4)
{
    memset(pkt, 0, pkt_len);
    /* eth header (14 bytes) */
    pkt[12] = 0x08; pkt[13] = 0x00;   /* ethertype IPv4 */
    /* ipv4 header (20 bytes starting at offset 14) */
    pkt[14] = 0x45;                    /* version=4, ihl=5 */
    pkt[15] = 0x00;                    /* DSCP/ECN */
    pkt[16] = (uint8_t)((pkt_len - 14) >> 8);   /* total length */
    pkt[17] = (uint8_t)(pkt_len - 14);
    pkt[22] = 64;                      /* TTL = 64 */
    pkt[23] = 6;                       /* proto = TCP */
    /* ipv4.src at offset 14+12 = 26 */
    pkt[26] = s1; pkt[27] = s2; pkt[28] = s3; pkt[29] = s4;
    /* ipv4.dst at offset 14+16 = 30 */
    pkt[30] = d1; pkt[31] = d2; pkt[32] = d3; pkt[33] = d4;
    return pkt_len;
}

int main(void)
{
    printf("=== Sketch Demo: Flow Frequency Estimation ===\n\n");

    rmt_pipeline_t *p = rmt_create();
    if (!p) { fprintf(stderr, "rmt_create failed\n"); return 1; }

    if (p4c_compile_file(p, "p4/sketch_demo.p4") < 0) {
        rmt_destroy(p); return 1;
    }
    if (rmt_load_entries(p, "entries/sketch_entries.txt") < 0) {
        rmt_destroy(p); return 1;
    }
    rmt_finalize(p);

    /* Print sketch stats before */
    printf("Before traffic:\n");
    for (int i = 0; i < p->nsketches; i++)
        rmt_sketch_print_stats(p->sketches[i]);
    printf("\n");

    rmt_pkt_t in, out;
    memset(&in, 0, sizeof(in));
    in.in_port = 1;

    /* Flow A: 10.0.0.1 -> 10.0.0.2, 100 packets */
    in.len = build_ipv4(in.data, 60, 10,0,0,1, 10,0,0,2);
    for (int i = 0; i < 100; i++)
        rmt_process(p, &in, &out);
    printf("Flow A: 10.0.0.1 -> 10.0.0.2, sent 100 packets\n");

    /* Flow B: 172.16.1.1 -> 172.16.1.2, 50 packets */
    in.len = build_ipv4(in.data, 60, 172,16,1,1, 172,16,1,2);
    for (int i = 0; i < 50; i++)
        rmt_process(p, &in, &out);
    printf("Flow B: 172.16.1.1 -> 172.16.1.2, sent 50 packets\n");

    /* Flow C: 192.168.1.1 -> 192.168.1.2, 200 packets (heavy hitter) */
    in.len = build_ipv4(in.data, 60, 192,168,1,1, 192,168,1,2);
    for (int i = 0; i < 200; i++)
        rmt_process(p, &in, &out);
    printf("Flow C: 192.168.1.1 -> 192.168.1.2, sent 200 packets (heavy hitter)\n\n");

    /* Print sketch stats after */
    printf("After traffic:\n");
    for (int i = 0; i < p->nsketches; i++)
        rmt_sketch_print_stats(p->sketches[i]);

    rmt_destroy(p);
    printf("\n=== Sketch Demo Complete ===\n");
    return 0;
}
