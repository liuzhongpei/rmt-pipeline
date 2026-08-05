/*
 * nc_demo.c - Service-Aware Network Coding Demo
 *
 * Demonstrates Algorithm 3 from the paper: differentiated Forward Error
 * Correction (FEC) driven by DNN classification results.
 *
 * Test Scenarios:
 *   1. GF(2^8) arithmetic verification (add, multiply, inverse)
 *   2. Systematic RLNC encoding: generate repair packets
 *   3. Decoding: recover lost data packets from repair packets
 *   4. Per-class coding parameter verification (VoIP/Video/Web/Bulk)
 *   5. Joint pipeline: classify → code → route (end-to-end latency)
 *   6. Bandwidth overhead analysis per service class
 *   7. Multi-flow coding with generation management
 *
 * The coding operates on packet payloads after classification:
 *   - VoIP (c2): Systematic RLNC, k=4, n=5, GF(2^8), 25% redundancy
 *   - Video (c1): Systematic RLNC, k=8, n=9, GF(2^8), 12.5% redundancy
 *   - Web (c0): XOR parity, k=8, n=9, GF(2), 12.5% redundancy
 *   - Bulk (c3): No coding (bypass), 0% redundancy
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <arpa/inet.h>
#include "rmt.h"

static int g_fails;

static void chk(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fails++;
}

/* ================================================================== */
/* GF(2^8) Arithmetic (irreducible polynomial: x^8 + x^4 + x^3 + x + 1) */
/* ================================================================== */
#define GF_POLY 0x11B  /* AES polynomial */

static uint8_t gf_log[256], gf_exp[512];

static void gf_init(void)
{
    /* Build log/exp tables for GF(2^8) with generator 3
     * (primitive element for irreducible polynomial x^8+x^4+x^3+x+1) */
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        /* x = x * 3 in GF(2^8): first compute x*2, then XOR with x */
        int doubled = x << 1;
        if (doubled & 0x100) doubled ^= 0x11B;  /* reduce mod polynomial */
        x = doubled ^ x;                        /* x*3 = x*2 + x */
    }
    gf_log[0] = 0;
    for (int i = 255; i < 512; i++)
        gf_exp[i] = gf_exp[i - 255];
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[(int)gf_log[a] + (int)gf_log[b]];
}

static uint8_t gf_inv(uint8_t a)
{
    if (a == 0) return 0;
    return gf_exp[255 - (int)gf_log[a]];
}

static uint8_t gf_add(uint8_t a, uint8_t b)
{
    return a ^ b;  /* addition in GF(2^8) = XOR */
}

/* ================================================================== */
/* Network Coding Parameters (Table 3b from paper)                     */
/* ================================================================== */
typedef enum { NC_NONE = 0, NC_XOR, NC_RLNC } nc_mode_t;

typedef struct {
    nc_mode_t mode;
    int k;          /* data packets per generation */
    int n;          /* total packets (data + repair) */
    const char *name;
} nc_params_t;

/* indexed by service class: c0=Web, c1=Video, c2=VoIP, c3=Bulk */
static const nc_params_t coding_table[4] = {
    { NC_XOR,  8, 9, "Web (XOR parity, k=8, n=9)" },
    { NC_RLNC, 8, 9, "Video (RLNC, k=8, n=9)" },
    { NC_RLNC, 4, 5, "VoIP (RLNC, k=4, n=5)" },
    { NC_NONE, 0, 0, "Bulk (no coding)" },
};

/* ================================================================== */
/* Generation Buffer (per-class packet accumulator)                    */
/* ================================================================== */
#define MAX_K       8
#define MAX_PKT_LEN 128  /* payload segment for coding */

typedef struct {
    uint8_t  pkts[MAX_K][MAX_PKT_LEN];
    int      pkt_lens[MAX_K];
    int      count;
    int      service_class;
    uint64_t generations_completed;
    uint64_t repair_packets_generated;
} gen_buffer_t;

/* ================================================================== */
/* RLNC Encoder: Systematic coding over GF(2^8)                       */
/* ================================================================== */
typedef struct {
    uint8_t data[MAX_PKT_LEN];
    int     len;
    uint8_t coefficients[MAX_K];  /* coding vector */
    bool    is_repair;
} coded_pkt_t;

/*
 * Generate (n-k) repair packets from k data packets using RLNC.
 * Coefficients are random elements of GF(2^8).
 * Systematic: original packets are forwarded as-is; repairs are additional.
 */
static int rlnc_encode(const gen_buffer_t *gen, const nc_params_t *params,
                       coded_pkt_t *repairs)
{
    int n_repairs = params->n - params->k;
    int payload_len = gen->pkt_lens[0]; /* assume uniform */

    for (int r = 0; r < n_repairs; r++) {
        memset(&repairs[r], 0, sizeof(coded_pkt_t));
        repairs[r].is_repair = true;
        repairs[r].len = payload_len;

        /* random coding coefficients */
        for (int j = 0; j < params->k; j++) {
            repairs[r].coefficients[j] = (uint8_t)(rand() % 255 + 1); /* non-zero */
        }

        /* linear combination: repair = sum(coeff[j] * pkt[j]) over GF(2^8) */
        for (int b = 0; b < payload_len; b++) {
            uint8_t val = 0;
            for (int j = 0; j < params->k; j++) {
                val = gf_add(val, gf_mul(repairs[r].coefficients[j],
                                         gen->pkts[j][b]));
            }
            repairs[r].data[b] = val;
        }
    }
    return n_repairs;
}

/*
 * XOR parity encoder: single parity packet = XOR of all k data packets.
 */
static int xor_encode(const gen_buffer_t *gen, const nc_params_t *params,
                      coded_pkt_t *repairs)
{
    int n_repairs = params->n - params->k;
    int payload_len = gen->pkt_lens[0];

    for (int r = 0; r < n_repairs; r++) {
        memset(&repairs[r], 0, sizeof(coded_pkt_t));
        repairs[r].is_repair = true;
        repairs[r].len = payload_len;
        for (int j = 0; j < params->k; j++)
            repairs[r].coefficients[j] = 1;  /* all ones for XOR */

        for (int b = 0; b < payload_len; b++) {
            uint8_t val = 0;
            for (int j = 0; j < params->k; j++)
                val ^= gen->pkts[j][b];
            repairs[r].data[b] = val;
        }
    }
    return n_repairs;
}

/* ================================================================== */
/* RLNC Decoder: Gaussian elimination over GF(2^8)                     */
/* ================================================================== */

/*
 * Recover lost data packets given (k-1) received data + 1 repair.
 * For the general case: given any k out of n received packets (data or
 * repair), solve the linear system to recover all k original packets.
 *
 * Simplified case: one packet lost, one repair available.
 */
static bool rlnc_decode_one_loss(const gen_buffer_t *gen, int lost_idx,
                                 const coded_pkt_t *repair,
                                 uint8_t *recovered, int payload_len)
{
    /* repair = sum(c[j] * pkt[j])
     * pkt[lost] = c[lost]^{-1} * (repair - sum_{j!=lost} c[j] * pkt[j]) */
    uint8_t c_inv = gf_inv(repair->coefficients[lost_idx]);
    if (c_inv == 0) return false;  /* singular - shouldn't happen with non-zero coeff */

    for (int b = 0; b < payload_len; b++) {
        uint8_t residual = repair->data[b];
        for (int j = 0; j < gen->count; j++) {
            if (j == lost_idx) continue;
            residual = gf_add(residual,
                              gf_mul(repair->coefficients[j], gen->pkts[j][b]));
        }
        recovered[b] = gf_mul(c_inv, residual);
    }
    return true;
}

static bool xor_decode_one_loss(const gen_buffer_t *gen, int lost_idx,
                                const coded_pkt_t *repair,
                                uint8_t *recovered, int payload_len)
{
    /* pkt[lost] = repair XOR all_other_pkts */
    for (int b = 0; b < payload_len; b++) {
        uint8_t val = repair->data[b];
        for (int j = 0; j < gen->count; j++) {
            if (j == lost_idx) continue;
            val ^= gen->pkts[j][b];
        }
        recovered[b] = val;
    }
    return true;
}

/* ================================================================== */
/* Test Scenarios                                                       */
/* ================================================================== */

/* T6.1: GF(2^8) arithmetic */
static void test_gf_arithmetic(void)
{
    printf("\n---- T6.1: GF(2^8) Arithmetic Verification ----\n");

    /* Verify multiplicative identity: a * 1 == a for all a */
    bool mul_id_ok = true;
    for (int i = 0; i < 256; i++) {
        if (gf_mul((uint8_t)i, 1) != (uint8_t)i) {
            printf("    failed at i=%d: gf_mul(%d,1)=%d\n",
                   i, i, gf_mul((uint8_t)i, 1));
            mul_id_ok = false;
            break;
        }
    }
    chk("gf_mul(x, 1) == x for all x", mul_id_ok);

    /* Verify inverse */
    bool inv_ok = true;
    for (int i = 1; i < 256; i++) {
        uint8_t inv = gf_inv((uint8_t)i);
        if (gf_mul((uint8_t)i, inv) != 1) {
            inv_ok = false;
            break;
        }
    }
    chk("gf_inv: a * a^{-1} == 1 for all a != 0", inv_ok);

    /* Verify distributive: a*(b+c) == a*b + a*c */
    bool dist_ok = true;
    for (int t = 0; t < 1000; t++) {
        uint8_t a = (uint8_t)(rand() % 256);
        uint8_t b = (uint8_t)(rand() % 256);
        uint8_t c = (uint8_t)(rand() % 256);
        uint8_t lhs = gf_mul(a, gf_add(b, c));
        uint8_t rhs = gf_add(gf_mul(a, b), gf_mul(a, c));
        if (lhs != rhs) { dist_ok = false; break; }
    }
    chk("distributive law: a*(b+c) == a*b + a*c", dist_ok);
}

/* T6.2: Per-class coding parameter verification */
static void test_coding_params(void)
{
    printf("\n---- T6.2: Per-Class Coding Parameters ----\n");
    printf("  %-6s %-30s k   n  redundancy\n", "Class", "Mode");
    printf("  %-6s %-30s --- --- ----------\n", "-----", "----");

    for (int c = 0; c < 4; c++) {
        const nc_params_t *p = &coding_table[c];
        float redundancy = p->k > 0 ?
            100.0f * (float)(p->n - p->k) / (float)p->k : 0;
        printf("  c%-5d %-30s %-3d %-3d %.1f%%\n",
               c, p->name, p->k, p->n, redundancy);
    }

    chk("VoIP: k=4, n=5, RLNC",
        coding_table[2].k == 4 && coding_table[2].n == 5 &&
        coding_table[2].mode == NC_RLNC);
    chk("Video: k=8, n=9, RLNC",
        coding_table[1].k == 8 && coding_table[1].n == 9 &&
        coding_table[1].mode == NC_RLNC);
    chk("Web: k=8, n=9, XOR",
        coding_table[0].k == 8 && coding_table[0].n == 9 &&
        coding_table[0].mode == NC_XOR);
    chk("Bulk: no coding",
        coding_table[3].mode == NC_NONE);
}

/* T6.3: RLNC encode/decode correctness */
static void test_rlnc_correctness(void)
{
    printf("\n---- T6.3: RLNC Encode/Decode Correctness ----\n");

    int payload_len = 64;

    /* Test VoIP parameters: k=4, n=5 */
    printf("  --- VoIP (k=4, n=5, GF(2^8)) ---\n");
    gen_buffer_t gen;
    memset(&gen, 0, sizeof(gen));
    gen.count = 4;
    gen.service_class = 2;

    /* Fill generation with random data packets */
    for (int j = 0; j < 4; j++) {
        gen.pkt_lens[j] = payload_len;
        for (int b = 0; b < payload_len; b++)
            gen.pkts[j][b] = (uint8_t)(rand() % 256);
    }

    /* Encode: generate 1 repair packet */
    coded_pkt_t repairs[4];
    int nr = rlnc_encode(&gen, &coding_table[2], repairs);
    chk("generated 1 repair packet", nr == 1);
    chk("repair packet has correct length", repairs[0].len == payload_len);
    chk("repair marked as coded", repairs[0].is_repair);

    /* Decode: simulate loss of packet 0, recover from repair */
    uint8_t recovered[MAX_PKT_LEN];
    bool ok = rlnc_decode_one_loss(&gen, 0, &repairs[0], recovered, payload_len);
    chk("decoder returned success", ok);
    chk("recovered packet matches original",
        memcmp(recovered, gen.pkts[0], (size_t)payload_len) == 0);

    /* Test all possible single-loss positions */
    bool all_recover = true;
    for (int lost = 0; lost < 4; lost++) {
        coded_pkt_t rep[4];
        rlnc_encode(&gen, &coding_table[2], rep);
        uint8_t rec[MAX_PKT_LEN];
        if (!rlnc_decode_one_loss(&gen, lost, &rep[0], rec, payload_len) ||
            memcmp(rec, gen.pkts[lost], (size_t)payload_len) != 0) {
            all_recover = false;
            printf("    [FAIL] recovery failed for lost packet %d\n", lost);
        }
    }
    chk("all single-loss positions recoverable", all_recover);

    /* Test Video parameters: k=8, n=9 */
    printf("  --- Video (k=8, n=9, GF(2^8)) ---\n");
    gen_buffer_t gen_vid;
    memset(&gen_vid, 0, sizeof(gen_vid));
    gen_vid.count = 8;
    gen_vid.service_class = 1;
    for (int j = 0; j < 8; j++) {
        gen_vid.pkt_lens[j] = payload_len;
        for (int b = 0; b < payload_len; b++)
            gen_vid.pkts[j][b] = (uint8_t)(rand() % 256);
    }

    coded_pkt_t vid_repairs[4];
    nr = rlnc_encode(&gen_vid, &coding_table[1], vid_repairs);
    chk("Video: generated 1 repair", nr == 1);

    all_recover = true;
    for (int lost = 0; lost < 8; lost++) {
        coded_pkt_t rep[4];
        rlnc_encode(&gen_vid, &coding_table[1], rep);
        uint8_t rec[MAX_PKT_LEN];
        if (!rlnc_decode_one_loss(&gen_vid, lost, &rep[0], rec, payload_len) ||
            memcmp(rec, gen_vid.pkts[lost], (size_t)payload_len) != 0)
            all_recover = false;
    }
    chk("Video: all 8 loss positions recoverable", all_recover);
}

/* T6.3b: XOR encode/decode */
static void test_xor_correctness(void)
{
    printf("\n---- T6.3b: XOR Parity Encode/Decode ----\n");
    int payload_len = 64;

    gen_buffer_t gen;
    memset(&gen, 0, sizeof(gen));
    gen.count = 8;
    gen.service_class = 0;  /* Web */
    for (int j = 0; j < 8; j++) {
        gen.pkt_lens[j] = payload_len;
        for (int b = 0; b < payload_len; b++)
            gen.pkts[j][b] = (uint8_t)(rand() % 256);
    }

    coded_pkt_t repairs[4];
    int nr = xor_encode(&gen, &coding_table[0], repairs);
    chk("XOR: generated 1 parity packet", nr == 1);

    /* Recover each possible single loss */
    bool all_ok = true;
    for (int lost = 0; lost < 8; lost++) {
        uint8_t rec[MAX_PKT_LEN];
        if (!xor_decode_one_loss(&gen, lost, &repairs[0], rec, payload_len) ||
            memcmp(rec, gen.pkts[lost], (size_t)payload_len) != 0)
            all_ok = false;
    }
    chk("XOR: all 8 single-loss positions recoverable", all_ok);
}

/* T6.4: Bandwidth overhead analysis */
static void test_bandwidth_overhead(void)
{
    printf("\n---- T6.4: Bandwidth Overhead Analysis ----\n");
    printf("  Service     Coding      Data/Gen  Repair/Gen  Overhead   Throughput Impact\n");
    printf("  ---------   ---------   --------  ----------  ---------  ----------------\n");

    int payload_len = 1200;  /* typical edge packet payload */
    int total_data_bytes = 100000;  /* simulate 100KB transfer */

    for (int c = 0; c < 4; c++) {
        const nc_params_t *p = &coding_table[c];
        if (p->mode == NC_NONE) {
            printf("  %-9s   %-9s   %-8s  %-10s  %-9s  no impact\n",
                   c == 3 ? "Bulk" : "?", "None", "N/A", "0", "0%");
            continue;
        }

        int data_per_gen = p->k * payload_len;
        int repair_per_gen = (p->n - p->k) * payload_len;
        float overhead_pct = 100.0f * (float)(p->n - p->k) / (float)p->k;
        int n_gens = total_data_bytes / data_per_gen;
        int total_repair = n_gens * repair_per_gen;
        float bw_factor = (float)(total_data_bytes + total_repair) /
                          (float)total_data_bytes;

        const char *svc = (c == 0) ? "Web" : (c == 1) ? "Video" :
                          (c == 2) ? "VoIP" : "Bulk";
        const char *mode = (p->mode == NC_XOR) ? "XOR" : "RLNC";
        printf("  %-9s   %-9s   %d×%dB    %d×%dB      %.1f%%      %.3f× data\n",
               svc, mode, p->k, payload_len,
               p->n - p->k, payload_len, overhead_pct, bw_factor);
    }

    chk("VoIP overhead = 25%",
        fabsf(100.0f * (float)(coding_table[2].n - coding_table[2].k) /
              (float)coding_table[2].k - 25.0f) < 0.1f);
    chk("Video overhead = 12.5%",
        fabsf(100.0f * (float)(coding_table[1].n - coding_table[1].k) /
              (float)coding_table[1].k - 12.5f) < 0.1f);
}

/* T7.1: Joint pipeline - classify + code + route
 * Self-contained: trains model, classifies, simulates coding decisions */
static void test_joint_pipeline(void)
{
    printf("\n---- T7.1: Joint Classify + Code + Route Pipeline ----\n");

    /* Set up the classification pipeline */
    rmt_pipeline_t *sw = rmt_create();
    if (p4c_compile_file(sw, "p4/enc_classify.p4") < 0 ||
        rmt_load_entries(sw, "entries/enc_classify.txt") < 0) {
        fprintf(stderr, "  FATAL: cannot set up pipeline\n");
        rmt_destroy(sw);
        g_fails++;
        return;
    }

    /* ---- Inline MLP training (same logic as enc_classify_demo) ---- */
    #define NIN  4
    #define NHID 24
    #define NOUT 4
    static const uint32_t scales[NIN] = { 1500, 65536, 16384, 256 };
    static float W1_nc[NHID * NIN], B1_nc[NHID];
    static float W2_nc[NOUT * NHID], B2_nc[NOUT];

    srand(42);
    float sinit = sqrtf(2.0f / (float)NIN);
    for (int j = 0; j < NHID; j++) {
        B1_nc[j] = 0;
        for (int i = 0; i < NIN; i++)
            W1_nc[j * NIN + i] = ((float)rand()/(float)RAND_MAX - 0.5f) * sinit;
    }
    float s2 = sqrtf(2.0f / (float)NHID);
    for (int k = 0; k < NOUT; k++) {
        B2_nc[k] = 0;
        for (int j = 0; j < NHID; j++)
            W2_nc[k * NHID + j] = ((float)rand()/(float)RAND_MAX - 0.5f) * s2;
    }

    /* inline gen_sample */
    #define rnd_nc(lo,hi) ((lo) + rand() % ((hi) - (lo) + 1))
    /* train 40 epochs x 6000 samples */
    for (int e = 0; e < 40; e++) {
        for (int n = 0; n < 6000; n++) {
            int label = rand() % NOUT;
            float x[NIN];
            uint64_t fraw[NIN] = {0};
            switch (label) {
            case 0: fraw[0]=rnd_nc(200,1460); fraw[1]=rnd_nc(16384,45000);
                    fraw[2]=rnd_nc(100,1400); fraw[3]=23; break;
            case 1: fraw[0]=rnd_nc(1200,1500); fraw[1]=rnd_nc(40000,65535);
                    fraw[2]=rnd_nc(1200,16000); fraw[3]=23; break;
            case 2: fraw[0]=rnd_nc(60,200); fraw[1]=rnd_nc(4096,16384);
                    fraw[2]=rnd_nc(20,160); fraw[3]=23; break;
            default: fraw[0]=rnd_nc(1400,1500); fraw[1]=rnd_nc(60000,65535);
                    fraw[2]=rnd_nc(14000,16384); fraw[3]=23; break;
            }
            for (int i = 0; i < NIN; i++) x[i] = (float)fraw[i] / (float)scales[i];

            /* forward */
            float h[NHID], yp[NOUT];
            for (int j = 0; j < NHID; j++) {
                float a = B1_nc[j];
                for (int i = 0; i < NIN; i++) a += W1_nc[j*NIN+i]*x[i];
                h[j] = a > 0 ? a : 0;
            }
            float mx = -1e30f, z[NOUT];
            for (int k2 = 0; k2 < NOUT; k2++) {
                z[k2] = B2_nc[k2];
                for (int j = 0; j < NHID; j++) z[k2] += W2_nc[k2*NHID+j]*h[j];
                if (z[k2] > mx) mx = z[k2];
            }
            float sum = 0;
            for (int k2 = 0; k2 < NOUT; k2++) { yp[k2]=expf(z[k2]-mx); sum+=yp[k2]; }
            for (int k2 = 0; k2 < NOUT; k2++) yp[k2] /= sum;

            /* backprop */
            float go[NOUT], gh[NHID];
            for (int k2 = 0; k2 < NOUT; k2++) go[k2] = yp[k2] - (k2==label?1.0f:0.0f);
            for (int j = 0; j < NHID; j++) {
                gh[j] = 0;
                for (int k2 = 0; k2 < NOUT; k2++) gh[j] += go[k2]*W2_nc[k2*NHID+j];
                if (h[j] <= 0) gh[j] = 0;
            }
            for (int k2 = 0; k2 < NOUT; k2++) {
                for (int j = 0; j < NHID; j++) W2_nc[k2*NHID+j] -= 0.05f*go[k2]*h[j];
                B2_nc[k2] -= 0.05f*go[k2];
            }
            for (int j = 0; j < NHID; j++) {
                for (int i = 0; i < NIN; i++) W1_nc[j*NIN+i] -= 0.05f*gh[j]*x[i];
                B1_nc[j] -= 0.05f*gh[j];
            }
        }
    }
    printf("  Model trained (4-%d-%d MLP, 40 epochs)\n", NHID, NOUT);
    rmt_dnn_load(sw->dnns[0], NHID, NOUT, W1_nc, B1_nc, W2_nc, B2_nc, scales);

    /* ---- Benchmark: classify + coding decision + route ---- */
    int npkts = 10000;
    int class_counts[4] = {0};
    int coded_counts[4] = {0};
    gen_buffer_t gen_bufs[4];
    memset(gen_bufs, 0, sizeof(gen_bufs));
    for (int c = 0; c < 4; c++) gen_bufs[c].service_class = c;

    struct timespec t0, t1;
    rmt_pkt_t in, out;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < npkts; i++) {
        int label = i % 4;
        uint64_t fraw[NIN] = {0};
        switch (label) {
        case 0: fraw[0]=rnd_nc(200,1460); fraw[1]=rnd_nc(16384,45000);
                fraw[2]=rnd_nc(100,1400); fraw[3]=23; break;
        case 1: fraw[0]=rnd_nc(1200,1500); fraw[1]=rnd_nc(40000,65535);
                fraw[2]=rnd_nc(1200,16000); fraw[3]=23; break;
        case 2: fraw[0]=rnd_nc(60,200); fraw[1]=rnd_nc(4096,16384);
                fraw[2]=rnd_nc(20,160); fraw[3]=23; break;
        default: fraw[0]=rnd_nc(1400,1500); fraw[1]=rnd_nc(60000,65535);
                fraw[2]=rnd_nc(14000,16384); fraw[3]=23; break;
        }

        /* Build TLS packet inline */
        memset(&in, 0, sizeof(in));
        uint8_t *d = in.data;
        memcpy(d, (uint8_t[]){0xaa,0,0,0,0,0x02}, 6);
        memcpy(d+6, (uint8_t[]){0x02,0,0,0,0,0x01}, 6);
        d[12]=0x08; d[13]=0x00;
        d[14]=0x45; d[16]=(uint8_t)(fraw[0]>>8); d[17]=(uint8_t)fraw[0];
        d[22]=64; d[23]=6;
        d[26]=192; d[27]=168; d[28]=1; d[29]=10;
        d[30]=10; d[31]=0; d[32]=0; d[33]=5;
        d[34]=0xC0; d[35]=0x00; d[36]=0x01; d[37]=0xBB;
        d[46]=0x50; d[47]=0x18;
        d[48]=(uint8_t)(fraw[1]>>8); d[49]=(uint8_t)fraw[1];
        d[54]=(uint8_t)fraw[3]; d[55]=0x03; d[56]=0x03;
        d[57]=(uint8_t)(fraw[2]>>8); d[58]=(uint8_t)fraw[2];
        in.len = 59; in.in_port = 1;

        rmt_process(sw, &in, &out);

        int cls = -1;
        uint8_t tos = out.data[15];
        if (tos == 0) cls = 0;
        else if (tos == 32) cls = 1;
        else if (tos == 46) cls = 2;
        else if (tos == 10) cls = 3;
        if (cls < 0) continue;

        class_counts[cls]++;

        /* Simulate NC stage (Algorithm 3) */
        const nc_params_t *params = &coding_table[cls];
        if (params->mode != NC_NONE) {
            gen_buffer_t *gb = &gen_bufs[cls];
            gb->count++;
            if (gb->count >= params->k) {
                coded_counts[cls] += (params->n - params->k);
                gb->generations_completed++;
                gb->repair_packets_generated += (uint64_t)(params->n - params->k);
                gb->count = 0;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t elapsed_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                        + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    printf("\n  Joint pipeline latency (classify + code_decision + route):\n");
    printf("    %d packets in %.3f ms\n", npkts, (double)elapsed_ns / 1e6);
    printf("    %.0f ns/pkt (%.3f Mpps)\n",
           (double)elapsed_ns / npkts,
           npkts / ((double)elapsed_ns / 1e3));

    printf("\n  Classification distribution:\n");
    const char *names[] = {"Web", "Video", "VoIP", "Bulk"};
    for (int c = 0; c < 4; c++)
        printf("    %-6s: %d packets, %d repair generated (%llu generations)\n",
               names[c], class_counts[c], coded_counts[c],
               (unsigned long long)gen_bufs[c].generations_completed);

    int total_repair = coded_counts[0] + coded_counts[1] +
                       coded_counts[2] + coded_counts[3];
    float total_overhead = 100.0f * (float)total_repair / (float)npkts;
    printf("\n  Total coding overhead: %d repair packets / %d data = %.1f%%\n",
           total_repair, npkts, total_overhead);
    printf("  (vs 25%% if uniform RLNC applied to all, or 0%% if no coding)\n");

    chk("joint pipeline processed all packets", class_counts[0] + class_counts[1] +
        class_counts[2] + class_counts[3] > npkts * 9 / 10);
    chk("Bulk traffic has zero coding overhead", coded_counts[3] == 0);
    chk("VoIP traffic has coding applied", coded_counts[2] > 0);
    chk("per-packet latency < 20000 ns",
        (double)elapsed_ns / npkts < 20000.0);

    rmt_destroy(sw);
    #undef NIN
    #undef NHID
    #undef NOUT
    #undef rnd_nc
}

/* T7.2: Coding throughput impact measurement */
static void test_coding_throughput(void)
{
    printf("\n---- T7.2: Coding Throughput Impact ----\n");

    int payload_len = 64;
    int n_gens = 10000;  /* number of generation cycles */

    /* Measure RLNC encoding throughput */
    gen_buffer_t gen;
    memset(&gen, 0, sizeof(gen));
    gen.count = 4;
    gen.service_class = 2;
    for (int j = 0; j < 4; j++) {
        gen.pkt_lens[j] = payload_len;
        for (int b = 0; b < payload_len; b++)
            gen.pkts[j][b] = (uint8_t)(rand() % 256);
    }

    coded_pkt_t repairs[4];
    struct timespec t0, t1;

    /* RLNC (VoIP): k=4, n=5 */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int g = 0; g < n_gens; g++)
        rlnc_encode(&gen, &coding_table[2], repairs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t rlnc4_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                       + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    /* RLNC (Video): k=8, n=9 */
    gen_buffer_t gen8;
    memset(&gen8, 0, sizeof(gen8));
    gen8.count = 8;
    gen8.service_class = 1;
    for (int j = 0; j < 8; j++) {
        gen8.pkt_lens[j] = payload_len;
        for (int b = 0; b < payload_len; b++)
            gen8.pkts[j][b] = (uint8_t)(rand() % 256);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int g = 0; g < n_gens; g++)
        rlnc_encode(&gen8, &coding_table[1], repairs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t rlnc8_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                       + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    /* XOR (Web): k=8, n=9 */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int g = 0; g < n_gens; g++)
        xor_encode(&gen8, &coding_table[0], repairs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t xor_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                    + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    printf("  Coding throughput (%d-byte payloads, %d generations):\n",
           payload_len, n_gens);
    printf("    RLNC k=4 (VoIP):   %.0f ns/generation, "
           "%.0f ns per repair pkt\n",
           (double)rlnc4_ns / n_gens, (double)rlnc4_ns / n_gens);
    printf("    RLNC k=8 (Video):  %.0f ns/generation, "
           "%.0f ns per repair pkt\n",
           (double)rlnc8_ns / n_gens, (double)rlnc8_ns / n_gens);
    printf("    XOR k=8 (Web):     %.0f ns/generation, "
           "%.0f ns per repair pkt\n",
           (double)xor_ns / n_gens, (double)xor_ns / n_gens);

    /* Per-packet amortized cost (repair cost / k data pkts) */
    double rlnc4_per_pkt = (double)rlnc4_ns / n_gens / 4.0;
    double rlnc8_per_pkt = (double)rlnc8_ns / n_gens / 8.0;
    double xor_per_pkt = (double)xor_ns / n_gens / 8.0;

    printf("\n  Amortized per-data-packet coding cost:\n");
    printf("    RLNC k=4 (VoIP):  %.1f ns/pkt\n", rlnc4_per_pkt);
    printf("    RLNC k=8 (Video): %.1f ns/pkt\n", rlnc8_per_pkt);
    printf("    XOR k=8 (Web):    %.1f ns/pkt\n", xor_per_pkt);
    printf("    None (Bulk):       0.0 ns/pkt\n");

    chk("RLNC coding cost < 5000 ns per data packet",
        rlnc4_per_pkt < 5000.0 && rlnc8_per_pkt < 5000.0);
    chk("XOR coding faster than RLNC", xor_per_pkt < rlnc8_per_pkt);
}

/* T7.3: Multi-generation stress test */
static void test_multi_generation(void)
{
    printf("\n---- T7.3: Multi-Generation Stress Test ----\n");

    int payload_len = 64;
    int total_pkts = 40000;  /* 10K per class */

    gen_buffer_t bufs[4];
    memset(bufs, 0, sizeof(bufs));
    for (int c = 0; c < 4; c++) bufs[c].service_class = c;

    int repair_total[4] = {0};
    coded_pkt_t repairs[4];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < total_pkts; i++) {
        int cls = i % 4;
        const nc_params_t *p = &coding_table[cls];
        gen_buffer_t *gb = &bufs[cls];

        if (p->mode == NC_NONE) continue;

        /* Simulate adding packet to generation buffer */
        for (int b = 0; b < payload_len; b++)
            gb->pkts[gb->count][b] = (uint8_t)(rand() % 256);
        gb->pkt_lens[gb->count] = payload_len;
        gb->count++;

        if (gb->count >= p->k) {
            /* Encode */
            int nr;
            if (p->mode == NC_RLNC)
                nr = rlnc_encode(gb, p, repairs);
            else
                nr = xor_encode(gb, p, repairs);
            repair_total[cls] += nr;
            gb->generations_completed++;
            gb->count = 0;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    uint64_t elapsed_ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ULL
                        + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

    printf("  %d packets processed across 4 classes in %.3f ms\n",
           total_pkts, (double)elapsed_ns / 1e6);
    printf("\n  Per-class results:\n");
    const char *names[] = {"Web (XOR)", "Video (RLNC)", "VoIP (RLNC)", "Bulk (none)"};
    for (int c = 0; c < 4; c++) {
        printf("    %-14s: %llu generations, %d repair packets\n",
               names[c], (unsigned long long)bufs[c].generations_completed,
               repair_total[c]);
    }

    int data_per_class = total_pkts / 4;
    printf("\n  Expected generations: Web=%d, Video=%d, VoIP=%d, Bulk=0\n",
           data_per_class / 8, data_per_class / 8, data_per_class / 4);
    printf("  Expected repairs:     Web=%d, Video=%d, VoIP=%d, Bulk=0\n",
           data_per_class / 8, data_per_class / 8, data_per_class / 4);

    chk("Web generations match (10000/8 = 1250)",
        bufs[0].generations_completed == (uint64_t)(data_per_class / 8));
    chk("VoIP generations match (10000/4 = 2500)",
        bufs[2].generations_completed == (uint64_t)(data_per_class / 4));
    chk("Bulk has zero generations", bufs[3].generations_completed == 0);
    chk("total processing < 2000ms",
        (double)elapsed_ns / 1e6 < 2000.0);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    srand(42);
    gf_init();

    printf("=== Service-Aware Network Coding Demo ===\n");
    printf("    Algorithm 3: Differentiated FEC per service class\n");

    /* Core coding tests */
    test_gf_arithmetic();
    test_coding_params();
    test_rlnc_correctness();
    test_xor_correctness();
    test_bandwidth_overhead();

    /* Performance tests */
    test_coding_throughput();
    test_multi_generation();

    /* Joint pipeline test (classify + code + route) */
    test_joint_pipeline();

    printf("\n==================================================\n");
    printf("%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
