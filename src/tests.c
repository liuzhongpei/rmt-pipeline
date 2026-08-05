/*
 * tests.c - end-to-end tests for the software RMT pipeline
 *
 * For each of the three example dataplanes the test driver
 *   1. creates a fresh pipeline,
 *   2. compiles the P4 program onto it (p4c),
 *   3. loads the control-plane entries,
 *   4. injects hand-crafted packets and verifies the output bytes.
 *
 * Suites:
 *   mpls   multi-layer nested label stack (pop+pop+swap / transit swap)
 *   vxlan  VTEP gateway (decap -> overlay L2; encap -> underlay route)
 *   srv6   SRv6 End behavior + 5-tuple ACL
 */
#define _POSIX_C_SOURCE 199309L   /* clock_gettime with -std=c11 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <arpa/inet.h>
#include "rmt.h"

static int  g_fails;
static bool g_verbose;

/* ---------------- packet building helpers ---------------- */
typedef struct { uint8_t *p; int len; } bld_t;

static void put8(bld_t *b, uint8_t v)   { b->p[b->len++] = v; }
static void put16(bld_t *b, uint16_t v) { put8(b, (uint8_t)(v >> 8)); put8(b, (uint8_t)v); }
static void put32(bld_t *b, uint32_t v) { put16(b, (uint16_t)(v >> 16)); put16(b, (uint16_t)v); }
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
static void put_ip4(bld_t *b, const char *s)
{
    struct in_addr a;
    inet_pton(AF_INET, s, &a);
    put_bytes(b, &a.s_addr, 4);
}
static void put_ip6(bld_t *b, const char *s)
{
    struct in6_addr a;
    inet_pton(AF_INET6, s, &a);
    put_bytes(b, a.s6_addr, 16);
}
/* minimal IPv4 header (checksum 0) */
static void put_ipv4_hdr(bld_t *b, int payload, int proto,
                         const char *src, const char *dst)
{
    put8(b, 0x45); put8(b, 0);
    put16(b, (uint16_t)(20 + payload));
    put16(b, 1); put16(b, 0);
    put8(b, 64); put8(b, (uint8_t)proto); put16(b, 0);
    put_ip4(b, src);
    put_ip4(b, dst);
}
static void put_mpls(bld_t *b, uint32_t label, int s, int ttl)
{
    put32(b, (label << 12) | ((uint32_t)s << 8) | (uint32_t)ttl);
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

static void chk(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fails++;
}

static rmt_pipeline_t *load(const char *p4, const char *ent)
{
    printf("\n================ %s ================\n", p4);
    rmt_pipeline_t *pipe = rmt_create();
    pipe->verbose = g_verbose;
    if (p4c_compile_file(pipe, p4) < 0 || rmt_load_entries(pipe, ent) < 0) {
        fprintf(stderr, "FATAL: cannot set up %s\n", p4);
        rmt_destroy(pipe);
        return NULL;
    }
    return pipe;
}

static bool run(rmt_pipeline_t *pipe, rmt_pkt_t *in, rmt_pkt_t *out,
                const char *title)
{
    printf("\n---- %s ----\n", title);
    if (g_verbose)
        hexdump("ingress", in->data, in->len);
    if (rmt_process(pipe, in, out) < 0) {
        chk("pipeline processed packet", false);
        return false;
    }
    if (g_verbose && !out->dropped)
        hexdump("egress", out->data, out->len);
    return true;
}

/* ================= MPLS: nested label stack ================ */
static void test_mpls(void)
{
    rmt_pipeline_t *pipe = load("p4/mpls.p4", "entries/mpls_entries.txt");
    if (!pipe) { g_fails++; return; }

    rmt_pkt_t in, out;

    /* flow A: [100|200|300] -> pop, pop, swap 300->399, port 2 */
    memset(&in, 0, sizeof(in));
    bld_t b = { in.data, 0 };
    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "66:77:88:99:aa:bb");
    put16(&b, 0x8847);
    put_mpls(&b, 100, 0, 64);
    put_mpls(&b, 200, 0, 64);
    put_mpls(&b, 300, 1, 64);
    put_ipv4_hdr(&b, 4, 6, "10.1.1.1", "10.2.2.2");
    put_bytes(&b, "MPLS", 4);
    in.len = b.len;
    in.in_port = 1;

    if (run(pipe, &in, &out, "flow A: nested pop+pop+swap")) {
        chk("not dropped", !out.dropped);
        chk("two outer labels popped (len 50 -> 42)", out.len == 42);
        chk("ethertype still MPLS",
            out.data[12] == 0x88 && out.data[13] == 0x47);
        chk("inner label swapped 300 -> 399, s=1, ttl 63",
            out.data[14] == 0x00 && out.data[15] == 0x18 &&
            out.data[16] == 0xf1 && out.data[17] == 0x3f);
        chk("egress port 2", out.out_port == 2);
        chk("dst MAC rewritten for port 2", out.data[5] == 0x02);
        chk("IPv4 + payload intact",
            out.data[18] == 0x45 && !memcmp(out.data + 38, "MPLS", 4));
    }

    /* flow B: [500|600] -> transit swap 500->501, port 9 */
    memset(&in, 0, sizeof(in));
    b = (bld_t){ in.data, 0 };
    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "66:77:88:99:aa:bb");
    put16(&b, 0x8847);
    put_mpls(&b, 500, 0, 64);
    put_mpls(&b, 600, 1, 64);
    put_ipv4_hdr(&b, 4, 6, "10.1.1.1", "10.9.9.9");
    put_bytes(&b, "MPLS", 4);
    in.len = b.len;
    in.in_port = 1;

    if (run(pipe, &in, &out, "flow B: transit swap, inner untouched")) {
        chk("not dropped", !out.dropped);
        chk("stack depth kept (len 46)", out.len == 46);
        chk("top label swapped 500 -> 501, ttl 63",
            out.data[14] == 0x00 && out.data[15] == 0x1f &&
            out.data[16] == 0x50 && out.data[17] == 0x3f);
        chk("nested label 600 untouched (s=1, ttl 64)",
            out.data[18] == 0x00 && out.data[19] == 0x25 &&
            out.data[20] == 0x81 && out.data[21] == 0x40);
        chk("egress port 9", out.out_port == 9);
        chk("dst MAC rewritten for port 9", out.data[5] == 0x09);
    }

    rmt_destroy(pipe);
}

/* ==================== VXLAN gateway ======================== */
static void test_vxlan(void)
{
    rmt_pipeline_t *pipe = load("p4/vxlan.p4", "entries/vxlan_entries.txt");
    if (!pipe) { g_fails++; return; }

    rmt_pkt_t in, out;

    /* decap: outer eth/ipv4/udp/vxlan(vni 5000) + inner eth/ipv4 */
    memset(&in, 0, sizeof(in));
    bld_t b = { in.data, 0 };
    put_mac(&b, "52:54:00:00:00:01");             /* outer eth        */
    put_mac(&b, "52:54:00:00:00:02");
    put16(&b, 0x0800);
    put_ipv4_hdr(&b, 8 + 8 + 14 + 24, 17, "10.0.0.9", "10.0.0.1");
    put16(&b, 12345); put16(&b, 4789);            /* outer udp        */
    put16(&b, 8 + 8 + 14 + 24); put16(&b, 0);
    put8(&b, 0x08); put8(&b, 0); put16(&b, 0);    /* vxlan            */
    put8(&b, 0x00); put16(&b, 5000);              /* vni 5000         */
    put8(&b, 0);
    put_mac(&b, "02:00:00:00:00:07");             /* inner eth        */
    put_mac(&b, "02:00:00:00:00:01");
    put16(&b, 0x0800);
    put_ipv4_hdr(&b, 4, 6, "192.168.1.5", "172.16.0.7");
    put_bytes(&b, "PING", 4);
    in.len = b.len;
    in.in_port = 8;

    if (run(pipe, &in, &out, "decap: VNI 5000 -> overlay L2 bridge")) {
        chk("not dropped", !out.dropped);
        chk("outer headers stripped (len 88 -> 38)", out.len == 38);
        chk("output starts with inner dst MAC 02:..:07",
            out.data[0] == 0x02 && out.data[5] == 0x07);
        chk("inner ethertype IPv4",
            out.data[12] == 0x08 && out.data[13] == 0x00);
        chk("inner payload intact",
            !memcmp(out.data + out.len - 4, "PING", 4));
        chk("bridged to port 7 (inner MAC table)", out.out_port == 7);
    }

    /* encap: tenant IPv4 packet towards 192.168.1.0/24 */
    memset(&in, 0, sizeof(in));
    b = (bld_t){ in.data, 0 };
    put_mac(&b, "02:00:00:00:00:07");
    put_mac(&b, "02:00:00:00:00:05");
    put16(&b, 0x0800);
    put_ipv4_hdr(&b, 8, 6, "192.168.2.5", "192.168.1.10");
    put_bytes(&b, "TCPDATA!", 8);
    in.len = b.len;
    in.in_port = 3;

    if (run(pipe, &in, &out, "encap: tenant -> VXLAN VNI 5000")) {
        chk("not dropped", !out.dropped);
        chk("50-byte overhead added (len 42 -> 92)", out.len == 92);
        chk("outer MACs from underlay route",
            out.data[5] == 0x02 && out.data[11] == 0x01 &&
            out.data[0] == 0x52 && out.data[6] == 0x52);
        chk("outer IPv4 10.0.0.1 -> 10.0.0.2",
            !memcmp(out.data + 26, "\x0a\x00\x00\x01", 4) &&
            !memcmp(out.data + 30, "\x0a\x00\x00\x02", 4));
        chk("outer IPv4 len = tenant + 50 (78)",
            (out.data[16] << 8 | out.data[17]) == 78);
        chk("outer proto UDP, ttl 64",
            out.data[23] == 17 && out.data[22] == 64);
        chk("UDP dport 4789, len 58",
            (out.data[36] << 8 | out.data[37]) == 4789 &&
            (out.data[38] << 8 | out.data[39]) == 58);
        chk("VXLAN flags 0x08, VNI 5000",
            out.data[42] == 0x08 &&
            (out.data[46] << 16 | out.data[47] << 8 | out.data[48]) == 5000);
        chk("inner eth = original tenant eth",
            !memcmp(out.data + 50, in.data, 14));
        chk("inner IPv4 = original tenant IPv4",
            !memcmp(out.data + 64, in.data + 14, 20));
        chk("tenant payload intact",
            !memcmp(out.data + 84, "TCPDATA!", 8));
        chk("routed to underlay port 8", out.out_port == 8);
    }

    rmt_destroy(pipe);
}

/* ================== SRv6 End forwarding ==================== */
static void test_srv6(void)
{
    rmt_pipeline_t *pipe = load("p4/srv6.p4", "entries/srv6_entries.txt");
    if (!pipe) { g_fails++; return; }

    rmt_pkt_t in, out;

    /* SRv6 packet: dst = local SID fd00::2, SL=2, 3 segments */
    memset(&in, 0, sizeof(in));
    bld_t b = { in.data, 0 };
    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "66:77:88:99:aa:bb");
    put16(&b, 0x86dd);
    put8(&b, 0x60); put8(&b, 0); put16(&b, 0);
    put16(&b, 68); put8(&b, 43); put8(&b, 64);
    put_ip6(&b, "fd00::1");
    put_ip6(&b, "fd00::2");
    put8(&b, 17); put8(&b, 6); put8(&b, 4); put8(&b, 2);   /* SRH SL=2 */
    put8(&b, 2);  put8(&b, 0); put16(&b, 0);
    put_ip6(&b, "fd00::9");
    put_ip6(&b, "fd00::3");
    put_ip6(&b, "fd00::2");
    put16(&b, 5000); put16(&b, 6000); put16(&b, 12); put16(&b, 0);
    put_bytes(&b, "ABCD", 4);
    in.len = b.len;
    in.in_port = 1;

    if (run(pipe, &in, &out, "SRv6 End: SL 2->1, dst := seg[1]")) {
        struct in6_addr want;
        inet_pton(AF_INET6, "fd00::3", &want);
        chk("not dropped", !out.dropped);
        chk("IPv6 dst rewritten to next SID fd00::3",
            !memcmp(out.data + 38, want.s6_addr, 16));
        chk("SRH segments_left 2 -> 1", out.data[57] == 1);
        chk("hop_limit 64 -> 63", out.data[21] == 63);
        chk("routed to egress port 2", out.out_port == 2);
        chk("MACs rewritten",
            out.data[0] == 0x0a && out.data[11] == 0x01);
        chk("payload intact", !memcmp(out.data + out.len - 4, "ABCD", 4));
    }

    /* plain IPv6/UDP flow hitting the 5-tuple ACL deny rule */
    memset(&in, 0, sizeof(in));
    b = (bld_t){ in.data, 0 };
    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "de:ad:be:ef:00:01");
    put16(&b, 0x86dd);
    put8(&b, 0x60); put8(&b, 0); put16(&b, 0);
    put16(&b, 12); put8(&b, 17); put8(&b, 64);
    put_ip6(&b, "2001:db8::1");
    put_ip6(&b, "2001:db8::2");
    put16(&b, 1234); put16(&b, 80); put16(&b, 12); put16(&b, 0);
    put_bytes(&b, "WXYZ", 4);
    in.len = b.len;
    in.in_port = 4;

    if (run(pipe, &in, &out, "5-tuple ACL deny"))
        chk("packet dropped by ACL", out.dropped);

    rmt_destroy(pipe);
}

/* ============ SRv6 bulk forwarding: N varied packets ======== */
/*
 * Every packet targets the local SID fd00::2 and carries the
 * 3-SID list [fd00::9, fd00::3, fd00::2].  Variation per packet:
 *   - IPv6 src low bytes  = packet index
 *   - segments_left       = 2 or 1 (alternating)  ->  after End the
 *     new dst is seg[1]=fd00::3 (port 2) or seg[0]=fd00::9 (port 3)
 *   - UDP sport/dport and 8-byte payload derived from the index
 * Each output packet is verified field by field.
 */
static void build_srv6_pkt(rmt_pkt_t *in, int i)
{
    int sl = 2 - (i & 1);                 /* 2, 1, 2, 1, ... */

    memset(in, 0, sizeof(*in));
    bld_t b = { in->data, 0 };
    put_mac(&b, "00:11:22:33:44:55");
    put_mac(&b, "66:77:88:99:aa:bb");
    put16(&b, 0x86dd);
    put8(&b, 0x60); put8(&b, 0); put16(&b, 0);
    put16(&b, 72); put8(&b, 43); put8(&b, 64);   /* srh+3sid+udp+8 */
    put_ip6(&b, "fd00::1");
    b.p[b.len - 2] = (uint8_t)(i >> 8);          /* vary src low   */
    b.p[b.len - 1] = (uint8_t)i;
    put_ip6(&b, "fd00::2");                      /* dst: local SID */
    put8(&b, 17); put8(&b, 6); put8(&b, 4);
    put8(&b, (uint8_t)sl);                       /* segments_left  */
    put8(&b, 2);  put8(&b, 0); put16(&b, 0);
    put_ip6(&b, "fd00::9");                      /* seg[0]         */
    put_ip6(&b, "fd00::3");                      /* seg[1]         */
    put_ip6(&b, "fd00::2");                      /* seg[2]         */
    put16(&b, (uint16_t)(1024 + i));             /* udp sport      */
    put16(&b, (uint16_t)(2048 + i));             /* udp dport      */
    put16(&b, 16); put16(&b, 0);
    for (int j = 0; j < 8; j++)                  /* payload        */
        put8(&b, (uint8_t)(i + j));
    in->len = b.len;
    in->in_port = 1;
}

static const uint8_t *g_sid3, *g_sid9;           /* expected SIDs  */
static struct in6_addr g_sid3_a, g_sid9_a;

static void init_sids(void)
{
    inet_pton(AF_INET6, "fd00::3", &g_sid3_a);
    inet_pton(AF_INET6, "fd00::9", &g_sid9_a);
    g_sid3 = g_sid3_a.s6_addr;
    g_sid9 = g_sid9_a.s6_addr;
}

static bool check_srv6_out(const rmt_pkt_t *in, const rmt_pkt_t *out,
                           int i, int rc)
{
    int sl = 2 - (i & 1);
    const uint8_t *want_sid = (sl == 2) ? g_sid3 : g_sid9;
    int want_port = (sl == 2) ? 2 : 3;
    uint8_t payload[8];
    for (int j = 0; j < 8; j++)
        payload[j] = (uint8_t)(i + j);

    return rc == 0 && !out->dropped &&
           out->len == in->len &&
           !memcmp(out->data + 38, want_sid, 16) &&      /* dst  */
           out->data[57] == (uint8_t)(sl - 1) &&         /* SL   */
           out->data[21] == 63 &&                        /* hlim */
           out->out_port == want_port &&
           out->data[0] == 0x0a &&                       /* dmac */
           !memcmp(out->data + out->len - 8, payload, 8);
}

static void test_srv6_bulk(int n)
{
    rmt_pipeline_t *pipe = load("p4/srv6.p4", "entries/srv6_entries.txt");
    if (!pipe) { g_fails++; return; }

    init_sids();
    printf("\n---- SRv6 bulk: %d packets ----\n", n);

    rmt_pkt_t in, out;
    int bad = 0;
    uint64_t elapsed_ns = 0, total_bytes = 0;
    struct timespec t0, t1;

    for (int i = 0; i < n; i++) {
        build_srv6_pkt(&in, i);
        total_bytes += (uint64_t)in.len;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        int rc = rmt_process(pipe, &in, &out);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        elapsed_ns += (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                    + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

        if (!check_srv6_out(&in, &out, i, rc)) {
            bad++;
            if (bad <= 5)
                printf("  [FAIL] pkt #%d: rc=%d dropped=%d len=%d "
                       "SL=%u hlim=%u port=%d\n",
                       i, rc, out.dropped, out.len,
                       out.data[57], out.data[21], out.out_port);
        }
    }

    chk("all packets forwarded with correct End behavior", bad == 0);
    printf("  stats: %d packets, %d ok, %d bad\n", n, n - bad, bad);
    printf("         pipeline time %.3f ms total, %.0f ns/pkt, "
           "%.3f Mpps (%.1f Mbps @ %dB)\n",
           (double)elapsed_ns / 1e6, (double)elapsed_ns / n,
           n / ((double)elapsed_ns / 1e3),
           (double)total_bytes * 8 / ((double)elapsed_ns / 1e3),
           (int)(total_bytes / (uint64_t)n));

    /* ---- batch mode benchmark (no per-pkt clock_gettime) ---- */
    printf("\n---- SRv6 batch benchmark: %d packets x 64 batch ----\n", n);
    /* pool size: 128 packets (~260KB) fits comfortably in L2 cache,
     * avoiding memory-bandwidth bottleneck that skews timing.        */
    int nbatch = 128;
    rmt_pkt_t *batch_in  = calloc((size_t)nbatch, sizeof(rmt_pkt_t));
    rmt_pkt_t *batch_out = calloc((size_t)nbatch, sizeof(rmt_pkt_t));
    total_bytes = 0;
    for (int i = 0; i < nbatch; i++) {
        build_srv6_pkt(&batch_in[i], i);
        total_bytes += (uint64_t)batch_in[i].len;
    }
    total_bytes = total_bytes / (uint64_t)nbatch * (uint64_t)n;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int done = 0; done < n; ) {
        int cnt = (n - done) < RMT_BATCH_MAX ? (n - done) : RMT_BATCH_MAX;
        /* use pre-built packets, cycling through the pool */
        rmt_process_batch(pipe, &batch_in[done % nbatch], batch_out, cnt);
        done += cnt;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t batch_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                      + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
    printf("  batch  time %.3f ms total, %.0f ns/pkt, "
           "%.3f Mpps (%.1f Gbps @ %dB)\n",
           (double)batch_ns / 1e6, (double)batch_ns / n,
           n / ((double)batch_ns / 1e3),
           (double)total_bytes * 8 / ((double)batch_ns / 1e9) / 1e9,
           (int)(total_bytes / (uint64_t)n));
    free(batch_in);
    free(batch_out);

    rmt_destroy(pipe);
}

/* ========== SRv6 concurrent: 16 packets in parallel ========= */
/*
 * The pipeline configuration (parse graph, tries, ALU microcode) is
 * read-only after setup, and rmt_process() keeps all per-packet state
 * (PHV, output buffer) on the caller's stack -- so N packets can be
 * processed concurrently, one thread per packet, sharing one pipeline.
 * Models N independent RMT pipeline instances / N-packet inflight.
 */
typedef struct {
    rmt_pipeline_t *pipe;
    int      first, count, stride;       /* packet index range        */
    int      bad;
    uint64_t ns;
} par_arg_t;

static void *par_worker(void *v)
{
    par_arg_t *a = (par_arg_t *)v;
    rmt_pkt_t in, out;                    /* thread-private state      */
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < a->count; k++) {
        int i = a->first + k * a->stride;
        build_srv6_pkt(&in, i);
        int rc = rmt_process(a->pipe, &in, &out);
        if (!check_srv6_out(&in, &out, i, rc))
            a->bad++;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    a->ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
          + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
    return NULL;
}

static void test_srv6_parallel(int nthreads, int npkts)
{
    rmt_pipeline_t *pipe = load("p4/srv6.p4", "entries/srv6_entries.txt");
    if (!pipe) { g_fails++; return; }

    init_sids();
    printf("\n---- SRv6 concurrent: %d threads x %d packets ----\n",
           nthreads, npkts / nthreads);

    /* serial baseline over the same packet set */
    par_arg_t base = { pipe, 0, npkts, 1, 0, 0 };
    par_worker(&base);
    chk("serial baseline all correct", base.bad == 0);

    /* one pipeline shared by 16 threads, interleaved packet indices */
    pthread_t  th[64];
    par_arg_t  arg[64];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int t = 0; t < nthreads; t++) {
        arg[t] = (par_arg_t){ pipe, t, npkts / nthreads, nthreads, 0, 0 };
        pthread_create(&th[t], NULL, par_worker, &arg[t]);
    }
    int bad = 0;
    for (int t = 0; t < nthreads; t++) {
        pthread_join(th[t], NULL);
        bad += arg[t].bad;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t wall = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                  + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    chk("concurrent results all correct", bad == 0);
    printf("  stats: serial   %d pkts in %8.3f ms  (%.0f ns/pkt)\n",
           npkts, (double)base.ns / 1e6, (double)base.ns / npkts);
    printf("         parallel %d pkts in %8.3f ms  (%d threads, "
           "speedup %.1fx)\n",
           npkts, (double)wall / 1e6, nthreads,
           (double)base.ns / (double)wall);

    rmt_destroy(pipe);
}

/* ========== SRv6 multi-queue: RSS dispatch + worker rings ========= */
/*
 * Models a multi-queue NIC: the main thread RSS-hashes every packet
 * (over the IPv6 source address) onto one of N lock-free SPSC rings;
 * each ring is drained by one worker pinned to its own core, which
 * processes bursts of 64 via rmt_process_burst and verifies each
 * result in the completion callback (run-to-completion, zero-copy:
 * only 8-byte pointers cross the rings).
 */
typedef struct {
    uint64_t ok, bad;
    char     pad[48];                     /* one cache line per worker */
} mq_stat_t;

static void mq_verify_cb(int worker, const rmt_pkt_t *in,
                         rmt_pkt_t *out, void *arg)
{
    mq_stat_t *st = (mq_stat_t *)arg + worker;
    /* recover the packet index from the IPv6 src low bytes */
    int i = ((int)in->data[36] << 8) | (int)in->data[37];
    if (check_srv6_out(in, out, i, 0))
        st->ok++;
    else
        st->bad++;
}

static void test_srv6_mq(int nworkers, int npkts)
{
    rmt_pipeline_t *pipe = load("p4/srv6.p4", "entries/srv6_entries.txt");
    if (!pipe) { g_fails++; return; }

    init_sids();
    printf("\n---- SRv6 multi-queue: %d queues/workers, %d packets ----\n",
           nworkers, npkts);

    /* pre-built pool; index encoded in the src address (<= 16 bits) */
    int npool = 1024;
    rmt_pkt_t *pool = calloc((size_t)npool, sizeof(rmt_pkt_t));
    uint64_t total_bytes = 0;
    for (int i = 0; i < npool; i++) {
        build_srv6_pkt(&pool[i], i);
        total_bytes += (uint64_t)pool[i].len;
    }
    total_bytes = total_bytes / (uint64_t)npool * (uint64_t)npkts;

    struct timespec t0, t1;

    /* serial baseline: batch API over the same pool */
    rmt_pkt_t *outb = calloc(RMT_BATCH_MAX, sizeof(rmt_pkt_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int done = 0; done < npkts; ) {
        int cnt = (npkts - done) < RMT_BATCH_MAX
                  ? (npkts - done) : RMT_BATCH_MAX;
        rmt_process_batch(pipe, &pool[done % npool], outb, cnt);
        done += cnt;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t serial_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                       + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    /* multi-queue run: RSS dispatch, verify in worker callbacks */
    mq_stat_t *stats = calloc((size_t)nworkers, sizeof(mq_stat_t));
    rmt_mq_t *mq = rmt_mq_create(pipe, nworkers, 12, mq_verify_cb, stats);
    if (!mq || rmt_mq_start(mq) != 0) {
        chk("multi-queue engine started", false);
        rmt_mq_destroy(mq);
        free(pool); free(outb); free(stats);
        rmt_destroy(pipe);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < npkts; k++) {
        rmt_pkt_t *pkt = &pool[k % npool];
        /* 5-tuple flow hash (walks the SRH to the UDP ports) */
        uint32_t h = rmt_rss_flow_hash(pkt->data, pkt->len);
        rmt_mq_submit(mq, pkt, h);
    }
    rmt_mq_flush(mq);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t mq_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                   + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
    rmt_mq_stop(mq);

    uint64_t ok = 0, bad = 0;
    printf("  queue load:");
    for (int w = 0; w < nworkers; w++) {
        ok  += stats[w].ok;
        bad += stats[w].bad;
        printf(" q%d=%llu", w,
               (unsigned long long)rmt_mq_worker_processed(mq, w));
    }
    printf("\n");

    chk("all multi-queue packets verified",
        bad == 0 && ok == (uint64_t)npkts);
    printf("  stats: serial(batch) %8.3f ms  (%.0f ns/pkt, %.3f Mpps)\n",
           (double)serial_ns / 1e6, (double)serial_ns / npkts,
           npkts / ((double)serial_ns / 1e3));
    printf("         multi-queue   %8.3f ms  (%.0f ns/pkt, %.3f Mpps, "
           "%.2f Gbps @ %dB)\n",
           (double)mq_ns / 1e6, (double)mq_ns / npkts,
           npkts / ((double)mq_ns / 1e3),
           (double)total_bytes * 8 / ((double)mq_ns / 1e9) / 1e9,
           (int)(total_bytes / (uint64_t)npkts));
    printf("         speedup %.2fx with %d queues\n",
           (double)serial_ns / (double)mq_ns, nworkers);

    rmt_mq_destroy(mq);
    free(pool);
    free(outb);
    free(stats);
    rmt_destroy(pipe);
}

/* ========== TCP multi-queue: 5-tuple flow dispatch ========= */
/*
 * TCP traffic through the ids.p4 dataplane (eth/ipv4/tcp parse,
 * DNN stage idle without a model -> class 0 benign, IPv4 LPM
 * forwarding, TTL-1).  Dispatch key is rmt_rss_flow_hash(): the
 * full 5-tuple {src, dst, proto, sport, dport}.  Besides byte
 * verification, the callback asserts the RSS invariant -- every
 * flow must be seen by exactly one worker (no flow migration).
 */
#define TCPMQ_NFLOWS 2048

static _Atomic int g_flow_owner[TCPMQ_NFLOWS];   /* flow -> worker  */

/* eth(14) | ipv4(20) | tcp(20) = 54B; flow id f encoded in src    */
static void build_tcp_pkt(rmt_pkt_t *p, int f)
{
    memset(p, 0, sizeof(*p));
    uint8_t *d = p->data;
    memcpy(d,     (uint8_t[]){ 0xaa, 0, 0, 0, 0, 0x02 }, 6);
    memcpy(d + 6, (uint8_t[]){ 0x02, 0, 0, 0, 0, 0x01 }, 6);
    d[12] = 0x08; d[13] = 0x00;               /* IPv4               */
    d[14] = 0x45;                             /* ver/ihl            */
    d[17] = 40;                               /* total len          */
    d[22] = 64;                               /* ttl                */
    d[23] = 6;                                /* proto TCP          */
    d[26] = 10; d[27] = 1;                    /* src 10.1.f.f       */
    d[28] = (uint8_t)(f >> 8); d[29] = (uint8_t)f;
    if (f & 1) {                              /* dst -> route LPM   */
        d[30] = 10;  d[31] = 0;   d[32] = 0; d[33] = 5;   /* port 2 */
    } else {
        d[30] = 192; d[31] = 168; d[32] = 1; d[33] = 9;   /* port 3 */
    }
    d[34] = (uint8_t)((1024 + f) >> 8);       /* sport 1024+f       */
    d[35] = (uint8_t)(1024 + f);
    d[36] = 0; d[37] = (f & 1) ? 80 : 443;    /* dport              */
    d[46] = 0x50;                             /* data offset        */
    d[47] = 0x10;                             /* flags ACK          */
    d[48] = 0x20;                             /* window 8192        */
    p->len = 54;
    p->in_port = 1;
}

static bool check_tcp_out(const rmt_pkt_t *in, const rmt_pkt_t *out, int f)
{
    int want_port = (f & 1) ? 2 : 3;
    return !out->dropped && out->len == in->len &&
           out->data[22] == 63 &&                        /* ttl-1   */
           out->out_port == want_port &&
           !memcmp(out->data + 26, in->data + 26, 8) &&  /* addrs   */
           !memcmp(out->data + 34, in->data + 34, 4);    /* ports   */
}

typedef struct {
    uint64_t ok, bad, migrated;
    char     pad[40];                         /* one line per worker */
} tcpmq_stat_t;

static void tcp_mq_cb(int worker, const rmt_pkt_t *in,
                      rmt_pkt_t *out, void *arg)
{
    tcpmq_stat_t *st = (tcpmq_stat_t *)arg + worker;
    int f = ((int)in->data[28] << 8) | (int)in->data[29];

    if (check_tcp_out(in, out, f))
        st->ok++;
    else
        st->bad++;

    /* RSS invariant: first worker to see the flow owns it forever */
    int expect = -1;
    if (!atomic_compare_exchange_strong(&g_flow_owner[f], &expect,
                                        worker) && expect != worker)
        st->migrated++;
}

static void test_tcp_mq(int nworkers, int npkts)
{
    rmt_pipeline_t *pipe = load("p4/ids.p4", "entries/ids.txt");
    if (!pipe) { g_fails++; return; }

    printf("\n---- TCP multi-queue: %d flows -> %d queues, %d packets "
           "----\n", TCPMQ_NFLOWS, nworkers, npkts);

    rmt_pkt_t *pool = calloc(TCPMQ_NFLOWS, sizeof(rmt_pkt_t));
    uint64_t total_bytes = 0;
    for (int f = 0; f < TCPMQ_NFLOWS; f++) {
        build_tcp_pkt(&pool[f], f);
        total_bytes += (uint64_t)pool[f].len;
        atomic_store(&g_flow_owner[f], -1);
    }
    total_bytes = total_bytes / TCPMQ_NFLOWS * (uint64_t)npkts;

    struct timespec t0, t1;

    /* serial baseline over the same traffic */
    rmt_pkt_t *outb = calloc(RMT_BATCH_MAX, sizeof(rmt_pkt_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int done = 0; done < npkts; ) {
        int cnt = (npkts - done) < RMT_BATCH_MAX
                  ? (npkts - done) : RMT_BATCH_MAX;
        rmt_process_batch(pipe, &pool[done % TCPMQ_NFLOWS], outb, cnt);
        done += cnt;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t serial_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                       + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    tcpmq_stat_t *stats = calloc((size_t)nworkers, sizeof(tcpmq_stat_t));
    rmt_mq_t *mq = rmt_mq_create(pipe, nworkers, 12, tcp_mq_cb, stats);
    if (!mq || rmt_mq_start(mq) != 0) {
        chk("multi-queue engine started", false);
        rmt_mq_destroy(mq);
        free(pool); free(outb); free(stats);
        rmt_destroy(pipe);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int k = 0; k < npkts; k++) {
        rmt_pkt_t *pkt = &pool[k % TCPMQ_NFLOWS];
        rmt_mq_submit(mq, pkt, rmt_rss_flow_hash(pkt->data, pkt->len));
    }
    rmt_mq_flush(mq);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t mq_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                   + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
    rmt_mq_stop(mq);

    /* aggregate + flow distribution over the queues */
    uint64_t ok = 0, bad = 0, migrated = 0;
    uint64_t qmin = UINT64_MAX, qmax = 0;
    for (int w = 0; w < nworkers; w++) {
        ok += stats[w].ok;
        bad += stats[w].bad;
        migrated += stats[w].migrated;
        uint64_t n = rmt_mq_worker_processed(mq, w);
        if (n < qmin) qmin = n;
        if (n > qmax) qmax = n;
    }
    int owned = 0;
    for (int f = 0; f < TCPMQ_NFLOWS; f++)
        if (atomic_load(&g_flow_owner[f]) >= 0)
            owned++;

    chk("all tcp packets forwarded correctly",
        bad == 0 && ok == (uint64_t)npkts);
    chk("every flow handled by exactly one queue",
        migrated == 0 && owned == TCPMQ_NFLOWS);
    printf("  flow->queue: %d flows over %d queues, per-queue load "
           "min %llu / max %llu pkts\n",
           TCPMQ_NFLOWS, nworkers,
           (unsigned long long)qmin, (unsigned long long)qmax);
    printf("  stats: serial(batch) %8.3f ms  (%.0f ns/pkt, %.3f Mpps)\n",
           (double)serial_ns / 1e6, (double)serial_ns / npkts,
           npkts / ((double)serial_ns / 1e3));
    printf("         multi-queue   %8.3f ms  (%.0f ns/pkt, %.3f Mpps, "
           "%.2f Gbps @ %dB)\n",
           (double)mq_ns / 1e6, (double)mq_ns / npkts,
           npkts / ((double)mq_ns / 1e3),
           (double)total_bytes * 8 / ((double)mq_ns / 1e9) / 1e9,
           (int)(total_bytes / (uint64_t)npkts));
    printf("         speedup %.2fx with %d queues\n",
           (double)serial_ns / (double)mq_ns, nworkers);

    rmt_mq_destroy(mq);
    free(pool);
    free(outb);
    free(stats);
    rmt_destroy(pipe);
}

int main(int argc, char **argv)
{
    int bulk = 0, par = 0, mq = 0, tcpmq = 0;
    int mq_npkts = 200000, tcpmq_npkts = 400000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            g_verbose = true;
        else if (!strcmp(argv[i], "bulk"))
            bulk = (i + 1 < argc) ? atoi(argv[i + 1]) : 1000;
        else if (!strcmp(argv[i], "par")) {
            par = (i + 1 < argc && atoi(argv[i + 1]) > 0)
                  ? atoi(argv[i + 1]) : 16;
            if (par > 64)
                par = 64;
        } else if (!strcmp(argv[i], "mq")) {
            mq = (i + 1 < argc && atoi(argv[i + 1]) > 0)
                 ? atoi(argv[i + 1]) : 4;
            if (mq > 128)
                mq = 128;
            if (i + 2 < argc && atoi(argv[i + 2]) > 0)
                mq_npkts = atoi(argv[i + 2]);
        } else if (!strcmp(argv[i], "tcpmq")) {
            tcpmq = (i + 1 < argc && atoi(argv[i + 1]) > 0)
                    ? atoi(argv[i + 1]) : 8;
            if (tcpmq > 128)
                tcpmq = 128;
            if (i + 2 < argc && atoi(argv[i + 2]) > 0)
                tcpmq_npkts = atoi(argv[i + 2]);
        }
    }

    if (bulk > 0 || par > 0 || mq > 0 || tcpmq > 0) {
        if (bulk > 0)
            test_srv6_bulk(bulk);
        if (par > 0)
            test_srv6_parallel(par, 16000 / par * par);
        if (mq > 0)
            test_srv6_mq(mq, mq_npkts);
        if (tcpmq > 0)
            test_tcp_mq(tcpmq, tcpmq_npkts);
        printf("\n==================================================\n");
        printf("%s (%d failure%s)\n",
               g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
               g_fails, g_fails == 1 ? "" : "s");
        return g_fails ? 1 : 0;
    }

    test_mpls();
    test_vxlan();
    test_srv6();

    printf("\n==================================================\n");
    printf("%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
