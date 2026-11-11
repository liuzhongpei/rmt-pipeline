/*
 * pcap_test.c - Test RMT classification with real traffic (pcap)
 *
 * Reads a pcap file and feeds each packet through rmt_process().
 * Reports per-class classification accuracy, latency, and throughput.
 *
 * Usage: ./pcap_test <pcap_file> [labels_csv]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "rmt.h"

/* PCAP file format */
typedef struct {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} pcap_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_pkt_hdr_t;

#define MAX_PKT_SIZE 2048
#define MAX_PKTS     100000

/* DNN model parameters (4-24-4 MLP) */
#define NIN  4
#define NHID 24
#define NOUT 4

static float W1[NHID * NIN], B1[NHID];
static float W2[NOUT * NHID], B2[NOUT];
static uint32_t scales[NIN] = {1500, 65536, 16384, 256};

/* ---- Simplified inline training ---- */

static float relu_f(float x) { return x > 0 ? x : 0; }

static int mlp_predict(const float* x) {
    float h[NHID];
    for (int j = 0; j < NHID; j++) {
        float s = B1[j];
        for (int i = 0; i < NIN; i++) s += W1[j*NIN+i] * x[i];
        h[j] = relu_f(s);
    }
    float o[NOUT];
    for (int k = 0; k < NOUT; k++) {
        float s = B2[k];
        for (int j = 0; j < NHID; j++) s += W2[k*NHID+j] * h[j];
        o[k] = s;
    }
    int best = 0;
    for (int k = 1; k < NOUT; k++) if (o[k] > o[best]) best = k;
    return best;
}

static void gen_sample(int label, float* f) {
    /* Class distributions from published traffic measurements */
    static const float means[4][4] = {
        {600, 20000, 500, 23},    /* Web */
        {1380, 50000, 1320, 23},  /* Video */
        {130, 4500, 90, 23},      /* VoIP */
        {1460, 65535, 1440, 23},  /* Bulk */
    };
    static const float stds[4][4] = {
        {350, 8000, 350, 0},
        {80, 10000, 90, 0},
        {25, 1200, 25, 0},
        {15, 0, 15, 0},
    };
    for (int i = 0; i < NIN; i++) {
        float u1 = (float)rand() / RAND_MAX;
        float u2 = (float)rand() / RAND_MAX;
        if (u1 < 1e-9f) u1 = 1e-9f;
        float z = sqrtf(-2 * logf(u1)) * cosf(2 * 3.14159265f * u2);
        f[i] = means[label][i] + stds[label][i] * z;
        if (i < 3) f[i] = f[i] / scales[i];  /* normalize */
        else f[i] = f[i] / 256.0f;
    }
}

static void train_model(int nsamp, int epochs, float lr) {
    printf("  Training MLP on %d synthetic samples, %d epochs...\n", nsamp, epochs);

    /* Xavier init */
    float scale1 = sqrtf(6.0f / (NIN + NHID));
    float scale2 = sqrtf(6.0f / (NHID + NOUT));
    for (int i = 0; i < NHID*NIN; i++) W1[i] = scale1 * (2.0f * rand() / RAND_MAX - 1);
    for (int j = 0; j < NHID; j++) B1[j] = 0.01f * ((rand() % 100) - 50) / 50.0f;
    for (int i = 0; i < NOUT*NHID; i++) W2[i] = scale2 * (2.0f * rand() / RAND_MAX - 1);
    for (int k = 0; k < NOUT; k++) B2[k] = 0.01f * ((rand() % 100) - 50) / 50.0f;

    /* Training loop with early stopping. Seed the "best" snapshot with
     * the initial weights so the restore below is always defined, even
     * when the first epoch does not improve on best_acc (e.g. 0 epochs
     * or an empty/NaN training set). */
    float best_acc = 0;
    float best_W1[NHID*NIN], best_B1[NHID], best_W2[NOUT*NHID], best_B2[NOUT];
    memcpy(best_W1, W1, sizeof(best_W1));
    memcpy(best_B1, B1, sizeof(best_B1));
    memcpy(best_W2, W2, sizeof(best_W2));
    memcpy(best_B2, B2, sizeof(best_B2));

    for (int ep = 0; ep < epochs; ep++) {
        float loss = 0;
        int errs = 0;
        for (int s = 0; s < nsamp; s++) {
            int label = s % NOUT;
            float f[NIN];
            gen_sample(label, f);

            /* Forward */
            float h[NHID], o[NOUT], exp_o[NOUT];
            float sum_exp = 0, max_o = -1e30f;

            for (int j = 0; j < NHID; j++) {
                float v = B1[j];
                for (int i = 0; i < NIN; i++) v += W1[j*NIN+i] * f[i];
                h[j] = relu_f(v);
                if (h[j] > 5.0f) h[j] = 5.0f;
            }
            for (int k = 0; k < NOUT; k++) {
                float v = B2[k];
                for (int j = 0; j < NHID; j++) v += W2[k*NHID+j] * h[j];
                o[k] = v;
                if (v > max_o) max_o = v;
            }
            for (int k = 0; k < NOUT; k++) {
                exp_o[k] = expf(o[k] - max_o);
                sum_exp += exp_o[k];
            }

            /* Softmax + cross-entropy loss */
            float grad_o[NOUT];
            for (int k = 0; k < NOUT; k++) {
                float p = exp_o[k] / sum_exp;
                grad_o[k] = p - (k == label ? 1 : 0);
                loss += -(k == label ? logf(p + 1e-9f) : 0);
            }

            /* Backward with gradient clipping */
            float grad_h[NHID];
            memset(grad_h, 0, sizeof(grad_h));
            for (int k = 0; k < NOUT; k++) {
                float g = grad_o[k];
                if (g > 2.0f) g = 2.0f;
                if (g < -2.0f) g = -2.0f;
                B2[k] -= lr * g;
                for (int j = 0; j < NHID; j++) {
                    W2[k*NHID+j] -= lr * g * h[j];
                    grad_h[j] += grad_o[k] * W2[k*NHID+j];
                }
            }
            for (int j = 0; j < NHID; j++) {
                if (h[j] <= 0) continue;
                float g = grad_h[j];
                if (g > 1.0f) g = 1.0f;
                if (g < -1.0f) g = -1.0f;
                B1[j] -= lr * g;
                for (int i = 0; i < NIN; i++) {
                    W1[j*NIN+i] -= lr * g * f[i];
                }
            }

            if (mlp_predict(f) != label) errs++;
        }
        float acc = 100.0f * (nsamp - errs) / nsamp;
        if (ep % 10 == 0 || ep == epochs-1) {
            printf("    epoch %3d: loss=%.4f, acc=%.1f%%\n",
                   ep, loss/nsamp, acc);
        }
        /* Early stopping: save best weights */
        if (acc > best_acc) {
            best_acc = acc;
            memcpy(best_W1, W1, sizeof(W1));
            memcpy(best_B1, B1, sizeof(B1));
            memcpy(best_W2, W2, sizeof(W2));
            memcpy(best_B2, B2, sizeof(B2));
        }
        /* Stop if accuracy degrades significantly from best */
        if (ep > 20 && acc < best_acc - 5.0f) {
            printf("    Early stopping at epoch %d (best=%.1f%% at ~epoch %d)\n",
                   ep, best_acc, ep - (ep > 30 ? 20 : 10));
            break;
        }
    }
    /* Restore best weights */
    memcpy(W1, best_W1, sizeof(W1));
    memcpy(B1, best_B1, sizeof(B1));
    memcpy(W2, best_W2, sizeof(W2));
    memcpy(B2, best_B2, sizeof(B2));
    printf("    Final best training accuracy: %.1f%%\n", best_acc);
}

/* ToS byte to class mapping (matches P4 set() values directly) */
static int dscp_to_class(uint8_t tos) {
    /* P4 program sets: Web=0, Video=32, VoIP=46, Bulk=10 */
    if (tos == 0)  return 0;  /* Web */
    if (tos == 32) return 1;  /* Video */
    if (tos == 46) return 2;  /* VoIP */
    if (tos == 10) return 3;  /* Bulk */
    return -1;                  /* Unclassified */
}

static const char* class_names[] = {"Web", "Video", "VoIP", "Bulk", "Unclassified"};

/* Parse CSV labels file */
typedef struct {
    int pkt_idx;
    char label[32];
} label_t;

static int label_cmp(const void* a, const void* b) {
    const label_t* x = (const label_t*)a;
    const label_t* y = (const label_t*)b;
    return (x->pkt_idx > y->pkt_idx) - (x->pkt_idx < y->pkt_idx);
}

static label_t* load_labels(const char* filename, int* count) {
    FILE* f = fopen(filename, "r");
    if (!f) return NULL;

    label_t* labels = calloc(MAX_PKTS, sizeof(label_t));
    if (!labels) { fclose(f); return NULL; }
    char line[256];
    int n = 0;

    /* Skip header (empty file -> no samples) */
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        *count = 0;
        return labels;
    }

    while (fgets(line, sizeof(line), f) && n < MAX_PKTS) {
        int idx;
        int ipv4_len, tcp_win, tls_len, tls_type;
        char lbl[32];
        if (sscanf(line, "%d,%d,%d,%d,%d,%31s",
                   &idx, &ipv4_len, &tcp_win, &tls_len, &tls_type, lbl) == 6) {
            labels[n].pkt_idx = idx;
            strncpy(labels[n].label, lbl, 31);
            n++;
        }
    }
    fclose(f);
    /* the caller matches labels against packets with a monotonic cursor:
     * make that valid regardless of the CSV row order                 */
    qsort(labels, (size_t)n, sizeof(label_t), label_cmp);
    *count = n;
    return labels;
}

static int label_to_class(const char* label) {
    if (strcmp(label, "web") == 0) return 0;
    if (strcmp(label, "video") == 0) return 1;
    if (strcmp(label, "voip") == 0) return 2;
    if (strcmp(label, "bulk") == 0) return 3;
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pcap_file> [labels_csv]\n", argv[0]);
        return 1;
    }

    const char* pcap_file = argv[1];
    const char* labels_file = (argc >= 3) ? argv[2] : NULL;

    printf("=== RMT Real Traffic Classification Test ===\n");
    printf("  pcap:   %s\n", pcap_file);
    printf("  labels: %s\n\n", labels_file ? labels_file : "(none)");

    /* ---- Phase 1: Train model ---- */
    printf("--- Phase 1: Train DNN model ---\n");
    train_model(6000, 50, 0.01f);

    /* ---- Phase 2: Initialize RMT pipeline ---- */
    printf("\n--- Phase 2: Initialize RMT pipeline ---\n");
    rmt_pipeline_t* sw = rmt_create();
    if (!sw) { fprintf(stderr, "Failed to create pipeline\n"); return 1; }

    if (p4c_compile_file(sw, "p4/enc_classify.p4") != 0) {
        fprintf(stderr, "Failed to compile P4\n"); return 1;
    }
    printf("  [OK] P4 compiled\n");

    if (rmt_load_entries(sw, "entries/enc_classify.txt") != 0) {
        fprintf(stderr, "Failed to load entries\n"); return 1;
    }
    printf("  [OK] Entries loaded\n");

    /* Install trained model */
    if (rmt_dnn_load(sw->dnns[0], NHID, NOUT, W1, B1, W2, B2, scales) != 0) {
        fprintf(stderr, "Failed to install DNN\n"); return 1;
    }
    printf("  [OK] DNN model installed (Q16.16)\n");

    /* Load labels if provided */
    label_t* labels = NULL;
    int n_labels = 0;
    if (labels_file) {
        labels = load_labels(labels_file, &n_labels);
        printf("  Loaded %d ground truth labels\n", n_labels);
    }

    /* ---- Phase 3: Process pcap ---- */
    printf("\n--- Phase 3: Process pcap file ---\n");

    FILE* pcap = fopen(pcap_file, "rb");
    if (!pcap) { fprintf(stderr, "Failed to open: %s\n", pcap_file); return 1; }

    pcap_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, pcap) != 1) {
        fprintf(stderr, "Failed to read pcap header\n"); return 1;
    }
    printf("  Pcap: magic=0x%08x, link_type=%u\n", hdr.magic_number, hdr.network);

    rmt_pkt_t in, out;
    uint8_t pkt_buf[MAX_PKT_SIZE];

    int total_pkts = 0;
    int label_cur = 0;          /* cursor over the (sorted) labels CSV */
    int classified_pkts = 0;
    int correct = 0;
    int class_counts[5] = {0};
    int class_correct[4] = {0};
    int class_total[4] = {0};
    int tls_pkts = 0;

    struct timespec t_start, t_end;
    double total_latency_ns = 0;
    double min_latency_ns = 1e9;
    double max_latency_ns = 0;

    /* Per-ToS distribution for debugging */
    int tos_hist[256] = {0};

    pcap_pkt_hdr_t pkt_hdr;
    while (fread(&pkt_hdr, sizeof(pkt_hdr), 1, pcap) == 1) {
        if (pkt_hdr.incl_len > MAX_PKT_SIZE) {
            fseek(pcap, pkt_hdr.incl_len, SEEK_CUR);
            continue;
        }
        if (fread(pkt_buf, 1, pkt_hdr.incl_len, pcap) != pkt_hdr.incl_len) break;

        memset(&in, 0, sizeof(in));
        memset(&out, 0, sizeof(out));
        memcpy(in.data, pkt_buf, pkt_hdr.incl_len);
        in.len = pkt_hdr.incl_len;
        in.in_port = 1;

        clock_gettime(CLOCK_MONOTONIC, &t_start);
        rmt_process(sw, &in, &out);
        clock_gettime(CLOCK_MONOTONIC, &t_end);

        double lat = (t_end.tv_sec - t_start.tv_sec) * 1e9 +
                     (t_end.tv_nsec - t_start.tv_nsec);
        total_latency_ns += lat;
        if (lat < min_latency_ns) min_latency_ns = lat;
        if (lat > max_latency_ns) max_latency_ns = lat;

        total_pkts++;

        /* Check if TLS (port 443) */
        if (pkt_hdr.incl_len > 14 + 20 + 20 + 5) {
            uint16_t dport = (pkt_buf[14+20+2] << 8) | pkt_buf[14+20+3];
            if (dport == 443) tls_pkts++;
        }

        if (out.dropped) continue;

        uint8_t tos = out.data[15];
        tos_hist[tos]++;
        int predicted = dscp_to_class(tos);
        if (predicted >= 0 && predicted < 4) {
            classified_pkts++;
            class_counts[predicted]++;
        } else {
            class_counts[4]++;
        }

        /* Ground truth comparison: match on the packet index recorded in
         * the CSV (rows may be sparse; only pkt_idx is authoritative) */
        while (labels && label_cur < n_labels &&
               labels[label_cur].pkt_idx < total_pkts - 1)
            label_cur++;
        if (labels && label_cur < n_labels &&
            labels[label_cur].pkt_idx == total_pkts - 1) {
            int expected = label_to_class(labels[label_cur].label);
            if (expected >= 0) {
                class_total[expected]++;
                if (predicted == expected) {
                    correct++;
                    class_correct[expected]++;
                }
            }
            label_cur++;
        }

        if (total_pkts % 500 == 0) {
            printf("  Processed %d packets...\n", total_pkts);
        }
    }

    fclose(pcap);
    if (labels) free(labels);

    /* ---- Results ---- */
    printf("\n=== Results ===\n\n");
    printf("Total packets processed: %d\n", total_pkts);
    printf("TLS (port 443) packets:  %d\n", tls_pkts);
    printf("Classified packets:      %d\n", classified_pkts);
    if (total_pkts > 0) {
        printf("  Classification rate:   %.1f%% of total\n",
               100.0 * classified_pkts / total_pkts);

        printf("\nClassification distribution:\n");
        for (int i = 0; i < 5; i++) {
            printf("  %-13s: %5d (%.1f%%)\n", class_names[i], class_counts[i],
                   100.0 * class_counts[i] / total_pkts);
        }
    } else {
        printf("  (no packets to classify)\n");
    }

    /* ToS histogram */
    printf("\n  ToS byte histogram (top values):\n");
    for (int t = 0; t < 256; t++) {
        if (tos_hist[t] > 0) {
            printf("    ToS=0x%02x (%3d) : %5d pkts -> %s\n",
                   t, t, tos_hist[t],
                   t == 0 ? "Web (class 0)" :
                   t == 32 ? "Video (class 1)" :
                   t == 46 ? "VoIP (class 2)" :
                   t == 10 ? "Bulk (class 3)" : "no DSCP marking");
        }
    }

    if (labels) {
        printf("\nAccuracy (vs ground truth):\n");
        int total_labeled = 0;
        for (int i = 0; i < 4; i++) total_labeled += class_total[i];

        for (int i = 0; i < 4; i++) {
            if (class_total[i] > 0) {
                printf("  %-13s: %4d/%-4d (%.1f%%)\n", class_names[i],
                       class_correct[i], class_total[i],
                       100.0 * class_correct[i] / class_total[i]);
            } else {
                printf("  %-13s:    (no samples)\n", class_names[i]);
            }
        }
        printf("  Overall      : %4d/%-4d (%.1f%%)\n",
               correct, total_labeled,
               total_labeled > 0 ? 100.0 * correct / total_labeled : 0);
    }

    if (total_pkts > 0) {
        printf("\nLatency statistics:\n");
        printf("  Mean:   %7.0f ns/pkt\n", total_latency_ns / total_pkts);
        printf("  Min:    %7.0f ns/pkt\n", min_latency_ns);
        printf("  Max:    %7.0f ns/pkt\n", max_latency_ns);
        printf("  Throughput: %.2f Mpps\n",
               total_pkts / (total_latency_ns / 1e9) / 1e6);
    } else {
        printf("\nLatency statistics: (no packets)\n");
    }

    /* DNN stats */
    printf("\nDNN statistics:\n");
    rmt_dnn_print_stats(sw->dnns[0]);

    rmt_destroy(sw);
    printf("\nTest completed.\n");

    return 0;
}
