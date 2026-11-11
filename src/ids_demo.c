/*
 * ids_demo.c - DNN-based anomaly traffic detection on the RMT pipeline
 *
 * End-to-end ML-for-networking flow, all in one program:
 *
 *   1. control plane synthesizes labelled traffic (benign web/handshake
 *      vs SYN flood / Xmas scan / NULL scan) and trains a tiny MLP
 *      (4-16-2, float, SGD + softmax cross entropy)
 *   2. the model is quantized to Q16.16 and installed into the
 *      pipeline's dnn extern with rmt_dnn_load()
 *   3. packets stream through p4/ids.p4: stage 0 runs dnn_infer on
 *      {ipv4.len, ipv4.ttl, tcp.flags, tcp.window}, stage 1 drops
 *      anomalies, stage 2 forwards the rest -- no attack signature
 *      exists anywhere in the tables.
 *
 * Verified: single-packet verdicts per attack type, no false positive
 * on legitimate handshakes, batch detection/false-alarm rates.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rmt.h"

static int g_fails;

static void chk(const char *what, bool ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_fails++;
}

/* ---------------- synthetic traffic model ---------------- */
/* features: ipv4.len, ipv4.ttl, tcp.flags, tcp.window (raw) */
static const uint32_t scales[4] = { 1500, 256, 256, 65536 };

static int rnd(int lo, int hi) { return lo + rand() % (hi - lo + 1); }

static void gen_sample(int label, uint64_t f[4])
{
    if (label == 0) {
        switch (rand() % 3) {
        case 0:                          /* web data (ACK/PSH-ACK) */
            f[0] = rnd(200, 1460);  f[1] = (rand() & 1) ? 64 : 128;
            f[2] = (rand() & 1) ? 0x18 : 0x10; f[3] = rnd(8192, 65535);
            break;
        case 1:                          /* legitimate handshake SYN */
            f[0] = rnd(40, 60);     f[1] = (rand() & 1) ? 64 : 128;
            f[2] = 0x02;            f[3] = rnd(29200, 65535);
            break;
        default:                         /* FIN-ACK teardown */
            f[0] = rnd(40, 60);     f[1] = (rand() & 1) ? 64 : 128;
            f[2] = 0x11;            f[3] = rnd(8192, 65535);
            break;
        }
    } else {
        switch (rand() % 3) {
        case 0:                          /* SYN flood (spoofed ttl,
                                            tiny window) */
            f[0] = 40; f[1] = rnd(32, 255); f[2] = 0x02;
            f[3] = rnd(0, 512);
            break;
        case 1:                          /* Xmas scan (FIN+PSH+URG) */
            f[0] = 40; f[1] = rnd(32, 255); f[2] = 0x29;
            f[3] = rnd(0, 1024);
            break;
        default:                         /* NULL scan (no flags) */
            f[0] = 40; f[1] = rnd(32, 255); f[2] = 0x00; f[3] = 0;
            break;
        }
    }
}

static void normalize(const uint64_t f[4], float x[4])
{
    for (int i = 0; i < 4; i++)
        x[i] = (float)f[i] / (float)scales[i];
}

/* ---------------- float MLP training (control plane) ---------------- */
#define NIN  4
#define NHID 16
#define NOUT 2

static float W1[NHID * NIN], B1[NHID], W2[NOUT * NHID], B2[NOUT];

static void forwardf(const float x[NIN], float h[NHID], float yp[NOUT])
{
    float z[NOUT];
    for (int j = 0; j < NHID; j++) {
        float a = B1[j];
        for (int i = 0; i < NIN; i++)
            a += W1[j * NIN + i] * x[i];
        h[j] = a > 0 ? a : 0;
    }
    float mx = -1e30f;
    for (int k = 0; k < NOUT; k++) {
        z[k] = B2[k];
        for (int j = 0; j < NHID; j++)
            z[k] += W2[k * NHID + j] * h[j];
        if (z[k] > mx)
            mx = z[k];
    }
    float sum = 0;
    for (int k = 0; k < NOUT; k++) {
        yp[k] = expf(z[k] - mx);
        sum += yp[k];
    }
    for (int k = 0; k < NOUT; k++)
        yp[k] /= sum;
}

static float frand(void) { return (float)rand() / (float)RAND_MAX - 0.5f; }

static float train(int nsamp, int epochs, float lr)
{
    for (int j = 0; j < NHID; j++) {
        B1[j] = 0;
        for (int i = 0; i < NIN; i++)
            W1[j * NIN + i] = frand();
    }
    for (int k = 0; k < NOUT; k++) {
        B2[k] = 0;
        for (int j = 0; j < NHID; j++)
            W2[k * NHID + j] = frand();
    }

    for (int e = 0; e < epochs; e++) {
        for (int n = 0; n < nsamp; n++) {
            int label = rand() & 1;
            uint64_t f[4];
            float x[NIN], h[NHID], yp[NOUT];
            gen_sample(label, f);
            normalize(f, x);
            forwardf(x, h, yp);

            float go[NOUT];                 /* d(CE)/d(logit)        */
            for (int k = 0; k < NOUT; k++)
                go[k] = yp[k] - (k == label ? 1.0f : 0.0f);
            float gh[NHID];
            for (int j = 0; j < NHID; j++) {
                gh[j] = 0;
                for (int k = 0; k < NOUT; k++)
                    gh[j] += go[k] * W2[k * NHID + j];
                if (h[j] <= 0)
                    gh[j] = 0;
            }
            for (int k = 0; k < NOUT; k++) {
                for (int j = 0; j < NHID; j++)
                    W2[k * NHID + j] -= lr * go[k] * h[j];
                B2[k] -= lr * go[k];
            }
            for (int j = 0; j < NHID; j++) {
                for (int i = 0; i < NIN; i++)
                    W1[j * NIN + i] -= lr * gh[j] * x[i];
                B1[j] -= lr * gh[j];
            }
        }
    }

    int ok = 0, ntest = 2000;               /* held-out accuracy     */
    for (int n = 0; n < ntest; n++) {
        int label = rand() & 1;
        uint64_t f[4];
        float x[NIN], h[NHID], yp[NOUT];
        gen_sample(label, f);
        normalize(f, x);
        forwardf(x, h, yp);
        if ((yp[1] > yp[0]) == (label == 1))
            ok++;
    }
    return (float)ok / (float)ntest;
}

/* ---------------- packet construction ---------------- */
/* eth(14) | ipv4(20) | tcp(20) */
static void mk_pkt(rmt_pkt_t *p, const uint64_t f[4], int proto,
                   uint32_t dst)
{
    memset(p, 0, sizeof(*p));
    uint8_t *d = p->data;
    memcpy(d, (uint8_t[]){ 0xaa, 0, 0, 0, 0, 0x02 }, 6);
    memcpy(d + 6, (uint8_t[]){ 0x02, 0, 0, 0, 0, 0x01 }, 6);
    d[12] = 0x08; d[13] = 0x00;
    d[14] = 0x45;                            /* ver/ihl              */
    d[16] = (uint8_t)(f[0] >> 8); d[17] = (uint8_t)f[0];   /* len    */
    d[22] = (uint8_t)f[1];                   /* ttl                  */
    d[23] = (uint8_t)proto;
    d[26] = 192; d[27] = 168; d[28] = 1; d[29] = 10;       /* src    */
    d[30] = (uint8_t)(dst >> 24); d[31] = (uint8_t)(dst >> 16);
    d[32] = (uint8_t)(dst >> 8);  d[33] = (uint8_t)dst;
    d[34] = 0x30; d[35] = 0x39;              /* sport 12345          */
    d[36] = 0x00; d[37] = 0x50;              /* dport 80             */
    d[46] = 0x50;                            /* data offset          */
    d[47] = (uint8_t)f[2];                   /* flags                */
    d[48] = (uint8_t)(f[3] >> 8); d[49] = (uint8_t)f[3];   /* window */
    p->len = 54;
    p->in_port = 1;
}

#define DST_A 0x0a000005u                    /* 10.0.0.5  -> port 2  */

int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && !strcmp(argv[1], "-v"));
    srand(7);

    printf("=== DNN in-network IDS: anomaly traffic detection ===\n\n");

    /* -- 1. control plane: train the MLP in float -- */
    printf("---- 1. train 4-16-2 MLP on synthetic traffic ----\n");
    float acc = train(4000, 30, 0.1f);
    printf("  held-out accuracy (float): %.1f%%\n", acc * 100.0f);
    chk("float model accuracy > 97%", acc > 0.97f);

    /* -- 2. compile the dataplane, quantize + install the model -- */
    printf("\n---- 2. quantize (Q16.16) and install into the extern ----\n");
    rmt_pipeline_t *sw = rmt_create();
    if (!sw) { fprintf(stderr, "ids_demo: rmt_create failed\n"); return 1; }
    if (p4c_compile_file(sw, "p4/ids.p4") < 0 ||
        rmt_load_entries(sw, "entries/ids.txt") < 0)
        return 1;
    sw->verbose = verbose;
    chk("dnn extern declared in p4/ids.p4", sw->ndnns == 1);
    chk("model installed",
        rmt_dnn_load(sw->dnns[0], NHID, NOUT, W1, B1, W2, B2,
                     scales) == 0);

    int agree = 0, nq = 1000;                /* quantization check   */
    for (int n = 0; n < nq; n++) {
        int label = rand() & 1;
        uint64_t f[4];
        float x[NIN], h[NHID], yp[NOUT];
        gen_sample(label, f);
        normalize(f, x);
        forwardf(x, h, yp);
        int cf = yp[1] > yp[0];
        if (rmt_dnn_infer(sw->dnns[0], f) == cf)
            agree++;
    }
    printf("  float vs fixed-point agreement: %.1f%%\n",
           100.0f * (float)agree / (float)nq);
    chk("quantized model agrees with float > 99%", agree >= nq * 99 / 100);

    /* -- 3. single-packet verdicts through the pipeline -- */
    printf("\n---- 3. per-packet verdicts (p4/ids.p4) ----\n");
    rmt_pkt_t in, out;

    uint64_t web[4] = { 1200, 64, 0x18, 32768 };
    mk_pkt(&in, web, 6, DST_A);
    rmt_process(sw, &in, &out);
    chk("benign web data -> forwarded (port 2, ttl-1)",
        !out.dropped && out.out_port == 2 && out.data[22] == 63);

    uint64_t syn[4] = { 44, 64, 0x02, 64240 };
    mk_pkt(&in, syn, 6, DST_A);
    rmt_process(sw, &in, &out);
    chk("legitimate SYN handshake -> forwarded (no false positive)",
        !out.dropped && out.out_port == 2);

    uint64_t flood[4] = { 40, 47, 0x02, 128 };
    mk_pkt(&in, flood, 6, DST_A);
    rmt_process(sw, &in, &out);
    chk("SYN flood packet -> classified anomaly, dropped", out.dropped);

    uint64_t xmas[4] = { 40, 201, 0x29, 512 };
    mk_pkt(&in, xmas, 6, DST_A);
    rmt_process(sw, &in, &out);
    chk("Xmas scan packet -> dropped", out.dropped);

    uint64_t nullscan[4] = { 40, 99, 0x00, 0 };
    mk_pkt(&in, nullscan, 6, DST_A);
    rmt_process(sw, &in, &out);
    chk("NULL scan packet -> dropped", out.dropped);

    uint64_t udalike[4] = { 300, 64, 0, 0 };
    mk_pkt(&in, udalike, 17, DST_A);         /* UDP: IDS not applied */
    rmt_process(sw, &in, &out);
    chk("non-TCP packet bypasses the IDS -> forwarded",
        !out.dropped && out.out_port == 2);

    /* -- 4. batch: mixed attack + benign stream -- */
    printf("\n---- 4. batch of 1000 packets (50%% attack mix) ----\n");
    int tp = 0, fp = 0, natk = 0, nben = 0;
    for (int n = 0; n < 1000; n++) {
        int label = rand() & 1;
        uint64_t f[4];
        gen_sample(label, f);
        mk_pkt(&in, f, 6, DST_A);
        rmt_process(sw, &in, &out);
        if (label) { natk++; if (out.dropped)  tp++; }
        else       { nben++; if (out.dropped)  fp++; }
    }
    printf("  detection rate : %d/%d (%.1f%%)\n", tp, natk,
           100.0 * tp / natk);
    printf("  false alarms   : %d/%d (%.1f%%)\n", fp, nben,
           100.0 * fp / nben);
    chk("detection rate >= 97%", tp * 100 >= natk * 97);
    chk("false alarm rate <= 2%", fp * 100 <= nben * 2);

    printf("\ndnn statistics:\n");
    rmt_dnn_print_stats(sw->dnns[0]);

    rmt_destroy(sw);

    printf("\n%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
