/*
 * enc_classify_demo.c - MLP-based encrypted traffic classification
 *
 * End-to-end machine learning pipeline for classifying encrypted traffic:
 *
 *   1. Control plane synthesizes labelled encrypted traffic samples
 *      representing 4 application classes (web, video, voip, bulk)
 *   2. Trains a 4-24-4 MLP (float, SGD + softmax cross entropy)
 *   3. Quantizes to Q16.16 and installs into the DNN extern
 *   4. Packets flow through p4/enc_classify.p4: TLS app_data triggers
 *      DNN inference, QoS marking per class, then L3 forwarding
 *
 * Classification is based on observable features only:
 *   - ipv4.len      (packet size)
 *   - tcp.window    (receiver window)
 *   - tls.length    (TLS record payload length)
 *   - tls.type      (TLS content type)
 *
 * NO payload decryption. The model learns statistical patterns:
 *   - Web:   variable size (200-1460), medium window, variable TLS records
 *   - Video: large packets (1200-1500), large window, large TLS records
 *   - VoIP:  small fixed packets (60-200), small window, small TLS records
 *   - Bulk:  maximum MTU (1400-1500), very large window, max TLS records
 */
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
    if (!ok)
        g_fails++;
}

/* ============================================================ */
/* Synthetic encrypted traffic model                            */
/* Features: ipv4.len, tcp.window, tls.length, tls.type         */
/* ============================================================ */
#define NIN  4
#define NHID 24
#define NOUT 4

static const char *class_names[NOUT] = {
    "web", "video", "voip", "bulk"
};

/* normalization divisors (must match rmt_dnn_load scale) */
static const uint32_t scales[NIN] = { 1500, 65536, 16384, 256 };

static int rnd(int lo, int hi) { return lo + rand() % (hi - lo + 1); }

/*
 * Generate a labelled traffic sample.
 * Each class has distinctive statistical patterns in the 4 features.
 */
static void gen_sample(int label, uint64_t f[NIN])
{
    switch (label) {
    case 0: /* Web browsing (HTTPS) */
        /* Variable packet sizes, medium windows, app_data */
        f[0] = (uint64_t)rnd(200, 1460);       /* ipv4.len */
        f[1] = (uint64_t)rnd(16384, 45000);    /* tcp.window */
        f[2] = (uint64_t)rnd(100, 1400);       /* tls.length */
        f[3] = 23;                              /* tls.type = app_data */
        break;
    case 1: /* Video streaming */
        /* Large packets, large windows, large TLS records */
        f[0] = (uint64_t)rnd(1200, 1500);      /* mostly MTU-sized */
        f[1] = (uint64_t)rnd(40000, 65535);    /* large recv window */
        f[2] = (uint64_t)rnd(1200, 16000);     /* large TLS payload */
        f[3] = 23;
        break;
    case 2: /* VoIP / real-time */
        /* Small fixed packets, small windows, small TLS records */
        f[0] = (uint64_t)rnd(60, 200);         /* small voice frames */
        f[1] = (uint64_t)rnd(4096, 16384);     /* small window */
        f[2] = (uint64_t)rnd(20, 160);         /* tiny TLS payload */
        f[3] = 23;
        break;
    case 3: /* Bulk file transfer */
        /* Maximal packets, very large windows, max TLS records */
        f[0] = (uint64_t)rnd(1400, 1500);      /* always near MTU */
        f[1] = (uint64_t)rnd(60000, 65535);    /* maximum window */
        f[2] = (uint64_t)rnd(14000, 16384);    /* maximal TLS payload */
        f[3] = 23;
        break;
    }
}

static void normalize(const uint64_t f[NIN], float x[NIN])
{
    for (int i = 0; i < NIN; i++)
        x[i] = (float)f[i] / (float)scales[i];
}

/* ============================================================ */
/* Float MLP training (control plane)                           */
/* ============================================================ */
static float W1[NHID * NIN], B1[NHID], W2[NOUT * NHID], B2[NOUT];

static void forwardf(const float x[NIN], float h[NHID], float yp[NOUT])
{
    float z[NOUT];
    for (int j = 0; j < NHID; j++) {
        float a = B1[j];
        for (int i = 0; i < NIN; i++)
            a += W1[j * NIN + i] * x[i];
        h[j] = a > 0 ? a : 0;     /* ReLU */
    }
    float mx = -1e30f;
    for (int k = 0; k < NOUT; k++) {
        z[k] = B2[k];
        for (int j = 0; j < NHID; j++)
            z[k] += W2[k * NHID + j] * h[j];
        if (z[k] > mx) mx = z[k];
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

static int argmax(const float v[], int n)
{
    int best = 0;
    for (int i = 1; i < n; i++)
        if (v[i] > v[best]) best = i;
    return best;
}

/*
 * Rule-based baseline classifier (paper §5.3, "rule baseline").
 * Cascade of threshold rules derived directly from the configured
 * per-class feature ranges (paper Table 6); thresholds sit on the
 * configured range boundaries. No training or tuning is performed.
 *
 *   R1: voip   if len <= 200  && win <= 16384 && tls <= 160
 *   R2: bulk   if len >= 1400 && win >= 60000 && tls >= 14000
 *   R3: video  if >= 2 of { len >= 1200, win >= 40000, tls >= 1200 }
 *   R4: web    otherwise
 */
static int rule_classify(const uint64_t f[NIN])
{
    uint64_t len = f[0], win = f[1], tls = f[2];
    if (len <= 200 && win <= 16384 && tls <= 160)
        return 2;                                   /* voip box   */
    if (len >= 1400 && win >= 60000 && tls >= 14000)
        return 3;                                   /* bulk box   */
    int votes = (int)(len >= 1200) + (int)(win >= 40000)
              + (int)(tls >= 1200);
    if (votes >= 2)
        return 1;                                   /* video box  */
    return 0;                                       /* web (rest) */
}

static float train_impl(int nsamp, int epochs, float lr, bool holdout)
{
    /* Xavier-like initialization */
    float scale_init = sqrtf(2.0f / (float)NIN);
    for (int j = 0; j < NHID; j++) {
        B1[j] = 0;
        for (int i = 0; i < NIN; i++)
            W1[j * NIN + i] = frand() * scale_init;
    }
    float scale2 = sqrtf(2.0f / (float)NHID);
    for (int k = 0; k < NOUT; k++) {
        B2[k] = 0;
        for (int j = 0; j < NHID; j++)
            W2[k * NHID + j] = frand() * scale2;
    }

    for (int e = 0; e < epochs; e++) {
        for (int n = 0; n < nsamp; n++) {
            int label = rand() % NOUT;
            uint64_t f[NIN];
            float x[NIN], h[NHID], yp[NOUT];
            gen_sample(label, f);
            normalize(f, x);
            forwardf(x, h, yp);

            /* softmax cross-entropy gradient */
            float go[NOUT];
            for (int k = 0; k < NOUT; k++)
                go[k] = yp[k] - (k == label ? 1.0f : 0.0f);
            float gh[NHID];
            for (int j = 0; j < NHID; j++) {
                gh[j] = 0;
                for (int k = 0; k < NOUT; k++)
                    gh[j] += go[k] * W2[k * NHID + j];
                if (h[j] <= 0)
                    gh[j] = 0;  /* ReLU derivative */
            }
            /* update output layer */
            for (int k = 0; k < NOUT; k++) {
                for (int j = 0; j < NHID; j++)
                    W2[k * NHID + j] -= lr * go[k] * h[j];
                B2[k] -= lr * go[k];
            }
            /* update hidden layer */
            for (int j = 0; j < NHID; j++) {
                for (int i = 0; i < NIN; i++)
                    W1[j * NIN + i] -= lr * gh[j] * x[i];
                B1[j] -= lr * gh[j];
            }
        }
    }

    if (!holdout)
        return 0.0f;

    /* held-out accuracy */
    int ok = 0, ntest = 4000;
    for (int n = 0; n < ntest; n++) {
        int label = rand() % NOUT;
        uint64_t f[NIN];
        float x[NIN], h[NHID], yp[NOUT];
        gen_sample(label, f);
        normalize(f, x);
        forwardf(x, h, yp);
        if (argmax(yp, NOUT) == label)
            ok++;
    }
    return (float)ok / (float)ntest;
}

/* train and report held-out accuracy on the continuation of the
 * same PRNG stream (protocol A, used for the headline split) */
static float train(int nsamp, int epochs, float lr)
{
    return train_impl(nsamp, epochs, lr, true);
}

/* train only; the caller owns evaluation (protocol B, multi-seed) */
static void train_only(int nsamp, int epochs, float lr)
{
    (void)train_impl(nsamp, epochs, lr, false);
}

/* ============================================================ */
/* Packet construction                                          */
/* eth(14) | ipv4(20) | tcp(20) | tls(5) = 59 bytes            */
/* ============================================================ */
static void mk_tls_pkt(rmt_pkt_t *p, uint16_t iplen, uint16_t window,
                        uint16_t tls_len, uint8_t tls_type,
                        const char *dst)
{
    memset(p, 0, sizeof(*p));
    uint8_t *d = p->data;

    /* eth */
    memcpy(d, (uint8_t[]){0xaa,0,0,0,0,0x02}, 6);      /* dmac */
    memcpy(d+6, (uint8_t[]){0x02,0,0,0,0,0x01}, 6);    /* smac */
    d[12] = 0x08; d[13] = 0x00;                         /* ethertype IPv4 */

    /* ipv4 (20 bytes) */
    d[14] = 0x45;                                       /* ver+ihl */
    d[15] = 0;                                          /* tos (will be set by QoS) */
    d[16] = (uint8_t)(iplen >> 8); d[17] = (uint8_t)iplen;
    d[22] = 64;                                         /* ttl */
    d[23] = 6;                                          /* proto = TCP */
    /* src 192.168.1.10 */
    d[26] = 192; d[27] = 168; d[28] = 1; d[29] = 10;
    /* dst */
    struct in_addr a; inet_pton(AF_INET, dst, &a);
    uint32_t dv = ntohl(a.s_addr);
    d[30] = (uint8_t)(dv>>24); d[31] = (uint8_t)(dv>>16);
    d[32] = (uint8_t)(dv>>8);  d[33] = (uint8_t)dv;

    /* tcp (20 bytes) */
    d[34] = 0xC0; d[35] = 0x00;                        /* sport 49152 */
    d[36] = 0x01; d[37] = 0xBB;                        /* dport 443 */
    d[46] = 0x50;                                       /* data offset=5 */
    d[47] = 0x18;                                       /* flags PSH+ACK */
    d[48] = (uint8_t)(window >> 8); d[49] = (uint8_t)window;

    /* tls record header (5 bytes) */
    d[54] = tls_type;                                   /* content type */
    d[55] = 0x03; d[56] = 0x03;                        /* TLS 1.2 */
    d[57] = (uint8_t)(tls_len >> 8); d[58] = (uint8_t)tls_len;

    p->len = 59;
    p->in_port = 1;
}

/* ============================================================ */
/* Main                                                         */
/* ============================================================ */
int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && !strcmp(argv[1], "-v"));
    srand(42);

    printf("=== Encrypted Traffic Classification (MLP 4-%d-%d) ===\n\n",
           NHID, NOUT);

    /* ---- 1. Train the MLP ---- */
    printf("---- 1. train %d-%d-%d MLP on synthetic encrypted traffic ----\n",
           NIN, NHID, NOUT);
    float acc = train(6000, 40, 0.05f);
    printf("  held-out accuracy (float): %.1f%%\n", acc * 100.0f);
    chk("float model accuracy > 90%", acc > 0.90f);

    /* ---- 2. Compile pipeline, quantize + install model ---- */
    printf("\n---- 2. compile p4/enc_classify.p4 + install model ----\n");
    rmt_pipeline_t *sw = rmt_create();
    if (p4c_compile_file(sw, "p4/enc_classify.p4") < 0 ||
        rmt_load_entries(sw, "entries/enc_classify.txt") < 0) {
        fprintf(stderr, "FATAL: cannot set up pipeline\n");
        rmt_destroy(sw);
        return 1;
    }
    sw->verbose = verbose;
    chk("dnn extern declared", sw->ndnns == 1);
    chk("model installed (Q16.16)",
        rmt_dnn_load(sw->dnns[0], NHID, NOUT, W1, B1, W2, B2,
                     scales) == 0);

    /* quantization agreement check */
    int agree = 0, nq = 2000;
    for (int n = 0; n < nq; n++) {
        int label = rand() % NOUT;
        uint64_t f[NIN];
        float x[NIN], h[NHID], yp[NOUT];
        gen_sample(label, f);
        normalize(f, x);
        forwardf(x, h, yp);
        int cf = argmax(yp, NOUT);
        if (rmt_dnn_infer(sw->dnns[0], f) == cf)
            agree++;
    }
    printf("  float vs Q16.16 agreement: %.1f%%\n",
           100.0f * (float)agree / (float)nq);
    chk("quantization agreement > 95%", agree >= nq * 95 / 100);

    /* ---- 3. Single-packet classification verdicts ---- */
    printf("\n---- 3. per-class single-packet verdicts ----\n");
    rmt_pkt_t in, out;

    /* Web browsing packet: medium size, medium window */
    mk_tls_pkt(&in, 600, 32000, 500, 23, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("web packet -> forwarded", !out.dropped);
    chk("web packet -> DSCP 0 (best effort)", out.data[15] == 0);
    printf("    class=%d tos=0x%02x port=%d\n",
           0, out.data[15], out.out_port);

    /* Video streaming: large packet, large window, large TLS (not max) */
    mk_tls_pkt(&in, 1350, 50000, 8000, 23, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("video packet -> forwarded", !out.dropped);
    /* video marking: tos = 32 (AF class 1) */
    int vid_tos = out.data[15];
    printf("    tos=0x%02x port=%d (expected tos=0x20 for video)\n",
           vid_tos, out.out_port);

    /* VoIP: small packet, small window, tiny TLS record */
    mk_tls_pkt(&in, 100, 8192, 60, 23, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("voip packet -> forwarded", !out.dropped);
    int voip_tos = out.data[15];
    printf("    class=%d tos=0x%02x port=%d\n",
           voip_tos == 46 ? 2 : -1, voip_tos, out.out_port);

    /* Bulk transfer: max MTU, max window, max TLS */
    mk_tls_pkt(&in, 1500, 65535, 16000, 23, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("bulk packet -> forwarded", !out.dropped);
    int bulk_tos = out.data[15];
    printf("    class=%d tos=0x%02x port=%d\n",
           bulk_tos == 10 ? 3 : -1, bulk_tos, out.out_port);

    /* Non-TLS packet (e.g. port 80): no classification applied */
    mk_tls_pkt(&in, 600, 32000, 500, 23, "10.0.0.5");
    in.data[36] = 0x00; in.data[37] = 0x50;  /* dport = 80, not 443 */
    rmt_process(sw, &in, &out);
    chk("non-443 packet bypasses classifier (tos=0)",
        !out.dropped && out.data[15] == 0);

    /* TLS handshake (type 22): not classified (only app_data = 23) */
    mk_tls_pkt(&in, 300, 32000, 200, 22, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("TLS handshake (type 22) not classified (tos=0)",
        !out.dropped && out.data[15] == 0);

    /* ---- 4. Batch classification accuracy ---- */
    printf("\n---- 4. batch of 4000 packets (balanced 4 classes) ----\n");
    int correct[NOUT] = {0}, total[NOUT] = {0};
    int total_correct = 0;
    static int cm[NOUT][NOUT];            /* MLP confusion matrix   */
    static int rcm[NOUT][NOUT];           /* rule confusion matrix  */
    static uint64_t batch_feats[4000][NIN];
    int rule_correct = 0;

    for (int n = 0; n < 4000; n++) {
        int label = rand() % NOUT;
        uint64_t f[NIN];
        gen_sample(label, f);
        memcpy(batch_feats[n], f, sizeof(f));
        mk_tls_pkt(&in, (uint16_t)f[0], (uint16_t)f[1],
                    (uint16_t)f[2], (uint8_t)f[3], "10.0.0.5");
        rmt_process(sw, &in, &out);

        /* determine predicted class from TOS field */
        int pred = -1;
        uint8_t tos = out.data[15];
        if (tos == 0)       pred = 0;  /* web */
        else if (tos == 32) pred = 1;  /* video */
        else if (tos == 46) pred = 2;  /* voip */
        else if (tos == 10) pred = 3;  /* bulk */

        total[label]++;
        if (pred >= 0)
            cm[label][pred]++;
        if (pred == label) {
            correct[label]++;
            total_correct++;
        }

        /* rule baseline on the identical feature vector */
        int rp = rule_classify(f);
        rcm[label][rp]++;
        if (rp == label)
            rule_correct++;
    }

    printf("  per-class accuracy:\n");
    for (int k = 0; k < NOUT; k++) {
        float pct = total[k] > 0 ? 100.0f * (float)correct[k] / (float)total[k] : 0;
        printf("    %-6s: %d/%d (%.1f%%)\n",
               class_names[k], correct[k], total[k], pct);
    }
    float overall = 100.0f * (float)total_correct / 4000.0f;
    printf("  overall  : %d/4000 (%.1f%%)\n", total_correct, overall);

    chk("overall classification accuracy > 85%", overall > 85.0f);
    for (int k = 0; k < NOUT; k++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s class accuracy > 70%%", class_names[k]);
        chk(buf, total[k] == 0 ||
            (100.0f * (float)correct[k] / (float)total[k]) > 70.0f);
    }

    /* confusion matrix (rows = true class, columns = predicted) */
    printf("  MLP confusion matrix (rows=true, cols=predicted):\n");
    printf("            %8s %8s %8s %8s\n",
           "web", "video", "voip", "bulk");
    for (int t = 0; t < NOUT; t++) {
        printf("    %-6s", class_names[t]);
        for (int p = 0; p < NOUT; p++)
            printf(" %8d", cm[t][p]);
        printf("\n");
    }
    printf("  MLP per-class precision:\n");
    for (int p = 0; p < NOUT; p++) {
        int colsum = 0;
        for (int t = 0; t < NOUT; t++)
            colsum += cm[t][p];
        float prec = colsum > 0 ?
            100.0f * (float)cm[p][p] / (float)colsum : 0.0f;
        printf("    %-6s: %d/%d (%.1f%%)\n",
               class_names[p], cm[p][p], colsum, prec);
    }

    /* rule baseline on the identical 4000 feature vectors */
    printf("  rule baseline (threshold cascade, no training):\n");
    printf("    overall: %d/4000 (%.2f%%)\n",
           rule_correct, 100.0f * (float)rule_correct / 4000.0f);
    for (int k = 0; k < NOUT; k++) {
        float pct = total[k] > 0 ?
            100.0f * (float)rcm[k][k] / (float)total[k] : 0.0f;
        printf("    %-6s recall: %d/%d (%.1f%%)\n",
               class_names[k], rcm[k][k], total[k], pct);
    }

    /* rule baseline decision cost (feature comparison only) */
    {
        struct timespec r0, r1;
        int sink = 0;
        clock_gettime(CLOCK_MONOTONIC, &r0);
        for (int rep = 0; rep < 250; rep++)
            for (int n = 0; n < 4000; n++)
                sink += rule_classify(batch_feats[n]);
        clock_gettime(CLOCK_MONOTONIC, &r1);
        uint64_t rns = (uint64_t)(r1.tv_sec - r0.tv_sec) * 1000000000ull
                     + (uint64_t)(r1.tv_nsec - r0.tv_nsec);
        printf("    rule decision cost: %.1f ns/decision "
               "(1,000,000 evaluations, sink=%d)\n",
               (double)rns / 1000000.0, sink);
    }

    /* ---- 5. Routing and MAC rewrite verification ---- */
    printf("\n---- 5. routing + MAC rewrite verification ----\n");
    mk_tls_pkt(&in, 600, 32000, 500, 23, "10.0.0.5");
    rmt_process(sw, &in, &out);
    chk("routed to port 2 (10.0.0.0/8)", out.out_port == 2);
    chk("ttl decremented (64 -> 63)", out.data[22] == 63);
    chk("dmac rewritten", memcmp(out.data,
        (uint8_t[]){0x0a,0x0b,0x0c,0x0d,0x0e,0x02}, 6) == 0);
    chk("smac rewritten", memcmp(out.data+6,
        (uint8_t[]){0x0a,0x0b,0x0c,0x0d,0x0e,0x01}, 6) == 0);

    mk_tls_pkt(&in, 600, 32000, 500, 23, "192.168.1.5");
    rmt_process(sw, &in, &out);
    chk("192.168.x.x routed to port 3", out.out_port == 3);

    mk_tls_pkt(&in, 600, 32000, 500, 23, "172.16.0.1");
    rmt_process(sw, &in, &out);
    chk("unroutable dst -> dropped", out.dropped);

    /* ---- 6. Multi-seed evaluation (protocol B) ----
     * For each seed: train on 6000 samples from stream srand(seed),
     * then re-seed with an independent stream (seed + 7919) before
     * drawing the 4000-sample test set, so test samples are not a
     * continuation of the training stream. */
    printf("\n---- 6. multi-seed evaluation (protocol B: "
           "independent test stream) ----\n");
    const unsigned seeds[5] = {42, 43, 44, 45, 46};
    double seed_acc[5];
    double recall_sum[NOUT] = {0};
    for (int si = 0; si < 5; si++) {
        srand(seeds[si]);
        train_only(6000, 40, 0.05f);
        rmt_dnn_load(sw->dnns[0], NHID, NOUT, W1, B1, W2, B2, scales);
        srand(seeds[si] + 7919u);   /* independent test stream */
        int ok = 0;
        int tot[NOUT] = {0}, cor[NOUT] = {0};
        for (int n = 0; n < 4000; n++) {
            int label = rand() % NOUT;
            uint64_t f[NIN];
            gen_sample(label, f);
            mk_tls_pkt(&in, (uint16_t)f[0], (uint16_t)f[1],
                        (uint16_t)f[2], (uint8_t)f[3], "10.0.0.5");
            rmt_process(sw, &in, &out);
            int pred = -1;
            uint8_t tos = out.data[15];
            if (tos == 0)       pred = 0;
            else if (tos == 32) pred = 1;
            else if (tos == 46) pred = 2;
            else if (tos == 10) pred = 3;
            tot[label]++;
            if (pred == label) { cor[label]++; ok++; }
        }
        seed_acc[si] = 100.0 * (double)ok / 4000.0;
        printf("  seed %u: overall %.2f%%  recall(", seeds[si], seed_acc[si]);
        for (int k = 0; k < NOUT; k++) {
            double r = tot[k] > 0 ?
                100.0 * (double)cor[k] / (double)tot[k] : 0.0;
            recall_sum[k] += r;
            printf("%s%.1f%%", k ? " " : "", r);
        }
        printf(")\n");
    }
    double ms_mean = 0, ms_var = 0;
    for (int si = 0; si < 5; si++)
        ms_mean += seed_acc[si];
    ms_mean /= 5.0;
    for (int si = 0; si < 5; si++)
        ms_var += (seed_acc[si] - ms_mean) * (seed_acc[si] - ms_mean);
    double ms_std = sqrt(ms_var / 5.0);
    printf("  protocol B mean accuracy: %.2f%% +/- %.2f%% "
           "(5 seeds, population std dev)\n", ms_mean, ms_std);
    printf("  protocol B mean per-class recall:");
    for (int k = 0; k < NOUT; k++)
        printf(" %s %.1f%%", class_names[k], recall_sum[k] / 5.0);
    printf("\n");
    chk("protocol B mean accuracy > 95%", ms_mean > 95.0);

    /* ---- 7. Per-packet service time (DNN inference enabled) ----
     * Pool of 1024 TLS application-data packets (126 B: 59 header
     * bytes + 67 payload bytes); all packets match the parser gate
     * (dport 443, tls.type 23) and therefore invoke DNN inference.
     * One clock_gettime call brackets each full loop. */
    printf("\n---- 7. per-packet service time (126-byte TLS app_data, "
           "DNN enabled) ----\n");
    {
#define LAT_POOL 1024
#define LAT_N    200000
        static rmt_pkt_t lat_pool[LAT_POOL];
        static rmt_pkt_t lat_out1;
        static rmt_pkt_t lat_bout[64];
        srand(1234u);
        for (int i = 0; i < LAT_POOL; i++) {
            int label = rand() % NOUT;
            uint64_t f[NIN];
            gen_sample(label, f);
            mk_tls_pkt(&lat_pool[i], (uint16_t)f[0], (uint16_t)f[1],
                        (uint16_t)f[2], (uint8_t)f[3], "10.0.0.5");
            lat_pool[i].len = 126;  /* header 59 B + payload 67 B */
        }
        rmt_process(sw, &lat_pool[0], &lat_out1);
        chk("latency packet forwarded (DNN invoked)", !lat_out1.dropped);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int n = 0; n < LAT_N; n++)
            rmt_process(sw, &lat_pool[n % LAT_POOL], &lat_out1);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        uint64_t ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
                    + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        printf("  single-packet: %.0f ns/pkt, %.2f Mpps, "
               "%.2f Gbps in-memory rate (126 B)\n",
               (double)ns / LAT_N, LAT_N / ((double)ns / 1e9) / 1e6,
               (double)LAT_N * 126.0 * 8.0 / ((double)ns / 1e9) / 1e9);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int done = 0; done < LAT_N; ) {
            int cnt = LAT_N - done > 64 ? 64 : LAT_N - done;
            rmt_process_batch(sw, &lat_pool[done % LAT_POOL],
                              lat_bout, cnt);
            done += cnt;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull
           + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        printf("  batch (64):    %.0f ns/pkt, %.2f Mpps, "
               "%.2f Gbps in-memory rate (126 B)\n",
               (double)ns / LAT_N, LAT_N / ((double)ns / 1e9) / 1e6,
               (double)LAT_N * 126.0 * 8.0 / ((double)ns / 1e9) / 1e9);
#undef LAT_POOL
#undef LAT_N
    }

    printf("\ndnn statistics:\n");
    rmt_dnn_print_stats(sw->dnns[0]);

    rmt_destroy(sw);

    printf("\n%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
