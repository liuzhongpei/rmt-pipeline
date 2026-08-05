/*
 * telescope_demo.c - MORP4-style dynamic network telescope demo
 *
 * Exercises p4/telescope.p4 as the single ingress/egress switch of an
 * organization (uplink port 1, LAN port 2, collector port 9):
 *
 *   1. inbound scans towards unused monitored addresses are captured
 *   2. an address activates with one outbound packet -> its inbound
 *      traffic is confidential and is never captured (correctness)
 *   3. epoch aging: idle addresses fall back into the telescope
 *   4. the same dataplane implements an IPv6 telescope
 *   5. batch /24 scan: telescope surface = all-but-active addresses,
 *      zero capture of traffic to active hosts
 *
 * Control plane responsibilities shown here: monitored prefixes
 * (entries/telescope.txt), aging TTL and epoch ticks (time
 * granularity of utilization inference).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rmt.h"

#define PORT_UPLINK    1
#define PORT_LAN       2
#define PORT_COLLECTOR 9

static int g_fails;

static void chk(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fails++;
}

/* eth(14) | ipv4(20) */
static void mk4(rmt_pkt_t *p, int in_port, uint32_t src, uint32_t dst)
{
    memset(p, 0, sizeof(*p));
    uint8_t *d = p->data;
    memcpy(d, (uint8_t[]){ 0xaa, 0, 0, 0, 0, 0x02 }, 6);
    memcpy(d + 6, (uint8_t[]){ 0x02, 0, 0, 0, 0, 0x01 }, 6);
    d[12] = 0x08; d[13] = 0x00;
    d[14] = 0x45;
    d[16] = 0; d[17] = 20 + 8;               /* total len            */
    d[22] = 64;                              /* ttl                  */
    d[23] = 17;                              /* udp                  */
    d[26] = (uint8_t)(src >> 24); d[27] = (uint8_t)(src >> 16);
    d[28] = (uint8_t)(src >> 8);  d[29] = (uint8_t)src;
    d[30] = (uint8_t)(dst >> 24); d[31] = (uint8_t)(dst >> 16);
    d[32] = (uint8_t)(dst >> 8);  d[33] = (uint8_t)dst;
    p->len = 42;                             /* + 8B payload         */
    p->in_port = in_port;
}

/* eth(14) | ipv6(40); a16 = last byte of the ::/48-prefixed address */
static void mk6(rmt_pkt_t *p, int in_port, uint8_t src_last,
                uint8_t dst_last)
{
    static const uint8_t pfx[6] = { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x09 };
    memset(p, 0, sizeof(*p));
    uint8_t *d = p->data;
    memcpy(d, (uint8_t[]){ 0xaa, 0, 0, 0, 0, 0x02 }, 6);
    memcpy(d + 6, (uint8_t[]){ 0x02, 0, 0, 0, 0, 0x01 }, 6);
    d[12] = 0x86; d[13] = 0xdd;
    d[14] = 0x60;                            /* version 6            */
    d[19] = 8;                               /* payload len          */
    d[20] = 17; d[21] = 64;                  /* next hdr, hop limit  */
    memcpy(d + 22, pfx, 6);  d[37] = src_last;   /* 2001:db8:9::X    */
    memcpy(d + 38, pfx, 6);  d[53] = dst_last;   /* 2001:db8:9::Y    */
    p->len = 62;                             /* + 8B payload         */
    p->in_port = in_port;
}

#define ADDR(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
     ((uint32_t)(c) << 8) | (uint32_t)(d))

static bool captured(const rmt_pkt_t *o)
{
    return !o->dropped && o->out_port == PORT_COLLECTOR;
}

static bool forwarded(const rmt_pkt_t *o, int port)
{
    return !o->dropped && o->out_port == port;
}

int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && !strcmp(argv[1], "-v"));

    printf("=== MORP4-style dynamic network telescope ===\n\n");

    rmt_pipeline_t *sw = rmt_create();
    if (p4c_compile_file(sw, "p4/telescope.p4") < 0 ||
        rmt_load_entries(sw, "entries/telescope.txt") < 0)
        return 1;
    sw->verbose = verbose;
    chk("utilization externs declared (util4 + util6)", sw->ncaches == 2);

    /* time granularity: an address stays "used" for 2 idle epochs */
    rmt_cache_t *u4 = sw->caches[0], *u6 = sw->caches[1];
    rmt_cache_set_aging(u4, 2);
    rmt_cache_set_aging(u6, 2);

    rmt_pkt_t in, out;
    uint32_t host  = ADDR(10, 9, 1, 7);      /* monitored, will wake */
    uint32_t dark  = ADDR(10, 9, 3, 3);      /* monitored, never up  */
    uint32_t exta  = ADDR(198, 51, 100, 4);  /* external scanner     */
    uint32_t noscp = ADDR(10, 8, 0, 1);      /* outside monitored    */

    /* -- 1. unused monitored space is the telescope -- */
    printf("---- 1. inbound scan to unused space -> captured ----\n");
    mk4(&in, PORT_UPLINK, exta, host);
    rmt_process(sw, &in, &out);
    chk("scan to 10.9.1.7 (never active) -> collector", captured(&out));

    mk4(&in, PORT_UPLINK, exta, noscp);
    rmt_process(sw, &in, &out);
    chk("traffic to unmonitored 10.8.0.1 -> forwarded, not captured",
        forwarded(&out, PORT_LAN));

    /* -- 2. activation + correctness -- */
    printf("\n---- 2. address activates -> its traffic is never "
           "captured ----\n");
    mk4(&in, PORT_LAN, host, exta);          /* first outbound pkt   */
    rmt_process(sw, &in, &out);
    chk("outbound from 10.9.1.7 -> uplink (and marked used)",
        forwarded(&out, PORT_UPLINK));

    mk4(&in, PORT_UPLINK, exta, host);       /* immediate reply      */
    rmt_process(sw, &in, &out);
    chk("inbound reply to 10.9.1.7 -> LAN, NOT captured (correctness)",
        forwarded(&out, PORT_LAN));

    mk4(&in, PORT_UPLINK, exta, dark);
    rmt_process(sw, &in, &out);
    chk("neighbor 10.9.3.3 still unused -> still captured",
        captured(&out));

    /* -- 3. epoch aging: idle addresses rejoin the telescope -- */
    printf("\n---- 3. aging (TTL 2 epochs) ----\n");
    rmt_cache_tick(u4);                      /* epoch 1: still live  */
    mk4(&in, PORT_UPLINK, exta, host);
    rmt_process(sw, &in, &out);
    chk("1 idle epoch -> 10.9.1.7 still used, forwarded",
        forwarded(&out, PORT_LAN));

    rmt_cache_tick(u4);                      /* epoch 2              */
    rmt_cache_tick(u4);                      /* epoch 3: expired     */
    mk4(&in, PORT_UPLINK, exta, host);
    rmt_process(sw, &in, &out);
    chk("3 idle epochs -> aged out, captured again", captured(&out));

    mk4(&in, PORT_LAN, host, exta);          /* wakes up again       */
    rmt_process(sw, &in, &out);
    mk4(&in, PORT_UPLINK, exta, host);
    rmt_process(sw, &in, &out);
    chk("re-activation -> immediately protected again",
        forwarded(&out, PORT_LAN));

    /* -- 4. the same dataplane is an IPv6 telescope -- */
    printf("\n---- 4. IPv6 telescope (2001:db8:9::/48) ----\n");
    mk6(&in, PORT_UPLINK, 0x99, 0x42);       /* ::42 never active    */
    rmt_process(sw, &in, &out);
    chk("scan to 2001:db8:9::42 -> captured", captured(&out));

    mk6(&in, PORT_LAN, 0x01, 0x99);          /* ::1 goes active      */
    rmt_process(sw, &in, &out);
    mk6(&in, PORT_UPLINK, 0x99, 0x01);
    rmt_process(sw, &in, &out);
    chk("2001:db8:9::1 active -> inbound forwarded, not captured",
        forwarded(&out, PORT_LAN));

    /* -- 5. batch: /24 scan against 5 active hosts -- */
    printf("\n---- 5. scan sweep of 10.9.2.0/24, 5 hosts active ----\n");
    const int active[5] = { 10, 20, 30, 40, 50 };
    for (int i = 0; i < 5; i++) {            /* hosts talk first     */
        mk4(&in, PORT_LAN, ADDR(10, 9, 2, (uint8_t)active[i]), exta);
        rmt_process(sw, &in, &out);
    }
    int ncap = 0, nfwd = 0, wrong = 0;
    for (int h = 1; h <= 254; h++) {         /* attacker sweeps /24  */
        mk4(&in, PORT_UPLINK, exta, ADDR(10, 9, 2, (uint8_t)h));
        rmt_process(sw, &in, &out);
        bool is_active = false;
        for (int i = 0; i < 5; i++)
            if (h == active[i])
                is_active = true;
        if (captured(&out))
            ncap++;
        else if (forwarded(&out, PORT_LAN))
            nfwd++;
        if (is_active == captured(&out))
            wrong++;                         /* active captured or   */
    }                                        /* dark forwarded       */
    printf("  captured %d / forwarded %d of 254 probes\n", ncap, nfwd);
    chk("telescope surface = 249 unused addresses", ncap == 249);
    chk("all 5 active hosts forwarded", nfwd == 5);
    chk("zero misclassified probes", wrong == 0);

    printf("\nutilization state:\n");
    printf("  util4: %d live addresses\n", rmt_cache_live(u4));
    printf("  util6: %d live addresses\n", rmt_cache_live(u6));
    rmt_cache_print_stats(u4);
    rmt_cache_print_stats(u6);

    rmt_destroy(sw);

    printf("\n%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
