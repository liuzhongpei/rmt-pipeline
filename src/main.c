/*
 * main.c - RMT pipeline demo: SRv6 End forwarding + 5-tuple ACL
 *
 *   1. compile p4/srv6.p4 onto the pipeline (parser, 4 stages,
 *      ALU microcode)
 *   2. install forwarding entries (control plane)
 *   3. inject an SRv6 packet destined to the local SID fd00::2 and
 *      verify the End behavior: SL 2->1, dst := seg[1] = fd00::3,
 *      hop_limit-1, route to port 2, MAC rewrite
 *   4. inject a plain IPv6/UDP packet matching the 5-tuple deny rule
 *      and verify it is dropped
 */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "rmt.h"

/* ---------------- packet building helpers ---------------- */
typedef struct { uint8_t *p; int len; } bld_t;

static void put8(bld_t *b, uint8_t v)   { b->p[b->len++] = v; }
static void put16(bld_t *b, uint16_t v) { put8(b, (uint8_t)(v >> 8)); put8(b, (uint8_t)v); }
static void put_bytes(bld_t *b, const void *d, int n)
{
    memcpy(b->p + b->len, d, (size_t)n);
    b->len += n;
}
static void put_mac(bld_t *b, const char *s)
{
    unsigned m[6];
    sscanf(s, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]);
    for (int i = 0; i < 6; i++)
        put8(b, (uint8_t)m[i]);
}
static void put_ip6(bld_t *b, const char *s)
{
    struct in6_addr a;
    inet_pton(AF_INET6, s, &a);
    put_bytes(b, a.s6_addr, 16);
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

/* Ethernet + IPv6 + SRH(3 SIDs) + UDP + payload */
static void build_srv6_pkt(rmt_pkt_t *pkt)
{
    bld_t b = { pkt->data, 0 };

    /* Ethernet */
    put_mac(&b, "00:11:22:33:44:55");        /* dmac (this hop)     */
    put_mac(&b, "66:77:88:99:aa:bb");        /* smac                */
    put16(&b, 0x86dd);

    /* IPv6: payload = SRH(8 + 3*16) + UDP(8) + payload(4) = 68     */
    put8(&b, 0x60); put8(&b, 0x00); put16(&b, 0x0000);  /* ver/tc/fl */
    put16(&b, 68);                            /* payload length      */
    put8(&b, 43);                             /* next header: SRH    */
    put8(&b, 64);                             /* hop limit           */
    put_ip6(&b, "fd00::1");                   /* src                 */
    put_ip6(&b, "fd00::2");                   /* dst = active SID    */

    /* SRH: next=UDP, ext_len=6, type=4, SL=2, last_entry=2         */
    put8(&b, 17); put8(&b, 6); put8(&b, 4); put8(&b, 2);
    put8(&b, 2);  put8(&b, 0); put16(&b, 0);
    put_ip6(&b, "fd00::9");                   /* seg[0] (final)      */
    put_ip6(&b, "fd00::3");                   /* seg[1] (next)       */
    put_ip6(&b, "fd00::2");                   /* seg[2] (this hop)   */

    /* UDP */
    put16(&b, 5000); put16(&b, 6000); put16(&b, 12); put16(&b, 0);

    /* payload */
    put_bytes(&b, "ABCD", 4);

    pkt->len = b.len;
    pkt->in_port = 1;
}

/* plain IPv6/UDP flow that hits the ACL deny rule */
static void build_udp_pkt(rmt_pkt_t *pkt)
{
    bld_t b = { pkt->data, 0 };

    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "de:ad:be:ef:00:01");
    put16(&b, 0x86dd);

    put8(&b, 0x60); put8(&b, 0x00); put16(&b, 0x0000);
    put16(&b, 12);
    put8(&b, 17);                             /* next header: UDP    */
    put8(&b, 64);
    put_ip6(&b, "2001:db8::1");
    put_ip6(&b, "2001:db8::2");

    put16(&b, 1234); put16(&b, 80); put16(&b, 12); put16(&b, 0);
    put_bytes(&b, "WXYZ", 4);

    pkt->len = b.len;
    pkt->in_port = 4;
}

static int check(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *p4file  = (argc > 1) ? argv[1] : "p4/srv6.p4";
    const char *entries = (argc > 2) ? argv[2] : "entries/srv6_entries.txt";

    rmt_pipeline_t *pipe = rmt_create();
    if (!pipe) { fprintf(stderr, "rmt_create failed\n"); return 1; }
    pipe->verbose = true;

    if (p4c_compile_file(pipe, p4file) < 0)
        return 1;
    if (rmt_load_entries(pipe, entries) < 0)
        return 1;

    int fails = 0;
    rmt_pkt_t in, out;

    /* ------------- test 1: SRv6 End forwarding ------------- */
    printf("\n########## Test 1: SRv6 End forwarding ##########\n");
    memset(&in, 0, sizeof(in));
    build_srv6_pkt(&in);
    hexdump("\ningress packet", in.data, in.len);
    printf("\n");

    if (rmt_process(pipe, &in, &out) < 0)
        return 1;

    hexdump("\negress packet", out.data, out.len);

    /* offsets: eth 0..13, ipv6 14..53 (hop_limit 21, dst 38..53),
     * srh 54..61 (SL at 57), seg list 62.. */
    struct in6_addr want;
    inet_pton(AF_INET6, "fd00::3", &want);

    printf("\nresults:\n");
    fails += check("packet not dropped", !out.dropped);
    fails += check("IPv6 dst rewritten to next SID fd00::3",
                   !memcmp(out.data + 38, want.s6_addr, 16));
    fails += check("SRH segments_left 2 -> 1", out.data[57] == 1);
    fails += check("hop_limit 64 -> 63", out.data[21] == 63);
    fails += check("routed to egress port 2", out.out_port == 2);
    fails += check("dst MAC rewritten (0a:0b:0c:0d:0e:0f)",
                   out.data[0] == 0x0a && out.data[5] == 0x0f);
    fails += check("src MAC rewritten (..:0e:01)", out.data[11] == 0x01);
    fails += check("payload preserved",
                   !memcmp(out.data + out.len - 4, "ABCD", 4));

    /* ------------- test 2: 5-tuple ACL deny ---------------- */
    printf("\n########## Test 2: 5-tuple ACL deny ##########\n");
    memset(&in, 0, sizeof(in));
    build_udp_pkt(&in);
    hexdump("\ningress packet", in.data, in.len);
    printf("\n");

    if (rmt_process(pipe, &in, &out) < 0)
        return 1;

    printf("\nresults:\n");
    fails += check("packet dropped by 5-tuple ACL", out.dropped);

    printf("\n%s (%d failure%s)\n",
           fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           fails, fails == 1 ? "" : "s");

    rmt_destroy(pipe);
    return fails ? 1 : 0;
}
