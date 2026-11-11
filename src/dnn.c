/*
 * dnn.c - DNN inference engine extern (quantized MLP)
 *
 * Software model of an in-switch inference block: a small multi-layer
 * perceptron (1 hidden ReLU layer + linear output, argmax class) whose
 * forward pass uses integer-only Q16.16 fixed-point arithmetic, i.e.
 * what a hardware MAC array next to the match-action stages would run.
 *
 * The control plane trains the model in floating point (see
 * ids_demo.c) and installs it with rmt_dnn_load(), which quantizes
 * weights/biases to Q16.16. Feature normalization is part of the
 * model: each raw input is divided by scale[i] on the way in.
 *
 * Inference is read-only w.r.t. the model, so packets on different
 * threads may infer concurrently; the statistics counters are shared
 * but updated with lock-free relaxed atomics (no mutex on the hot
 * path: per-inference locking serialized all workers here).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "rmt.h"

#define Q 16                    /* Q16.16 fixed point */

struct rmt_dnn {
    char name[RMT_NAME_LEN];
    int  nin, nhid, nout;
    int  in_fid[RMT_DNN_MAX_IN];         /* PHV fields bound in P4    */
    bool loaded;

    /* quantized parameters (Q16.16) */
    int32_t  w1[RMT_DNN_MAX_HID][RMT_DNN_MAX_IN];
    int32_t  b1[RMT_DNN_MAX_HID];
    int32_t  w2[RMT_DNN_MAX_OUT][RMT_DNN_MAX_HID];
    int32_t  b2[RMT_DNN_MAX_OUT];
    uint32_t scale[RMT_DNN_MAX_IN];      /* raw divisor per input     */

    /* statistics (lock-free; updated from many worker threads) */
    _Atomic uint64_t infers;
    _Atomic uint64_t class_cnt[RMT_DNN_MAX_OUT];
};

rmt_dnn_t *rmt_dnn_create(const char *name, int nin, const int *in_fids)
{
    if (nin < 1 || nin > RMT_DNN_MAX_IN)
        return NULL;
    rmt_dnn_t *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->nin = nin;
    for (int i = 0; i < nin; i++) {
        d->in_fid[i] = in_fids[i];
        d->scale[i] = 1;
    }
    return d;
}

void rmt_dnn_destroy(rmt_dnn_t *d)
{
    if (!d)
        return;
    free(d);
}

const char *rmt_dnn_name(const rmt_dnn_t *d)       { return d->name; }
int rmt_dnn_num_inputs(const rmt_dnn_t *d)         { return d->nin; }
int rmt_dnn_input_fid(const rmt_dnn_t *d, int i)   { return d->in_fid[i]; }

static int32_t quant(float x)
{
    float q = x * (float)(1 << Q);
    /* Clamp to ±2^30 so that w*xq (with xq clamped to ±2^29) fits in
     * int64_t: max product = 2^30 × 2^29 = 2^59, and 8 accumulations
     * give 2^62, safely below INT64_MAX (2^63). */
    if (q > 1073741824.0f)  q = 1073741824.0f;   /* 2^30 */
    if (q < -1073741824.0f) q = -1073741824.0f;
    return (int32_t)(q >= 0 ? q + 0.5f : q - 0.5f);
}

/* Load quantized weights into an existing DNN.
 *
 * Thread-safety: NOT safe to call concurrently with rmt_dnn_infer().
 * The caller must ensure all in-flight inferences complete before
 * reloading weights (e.g. stop workers, flush, reload, restart).
 * A future version may add RCU-style double-buffered weight sets
 * for atomic hot-swap (paper §6.2 claim). */
int rmt_dnn_load(rmt_dnn_t *d, int nhid, int nout,
                 const float *w1, const float *b1,
                 const float *w2, const float *b2,
                 const uint32_t *scale)
{
    if (!d || nhid < 1 || nhid > RMT_DNN_MAX_HID ||
        nout < 2 || nout > RMT_DNN_MAX_OUT)
        return -1;
    d->nhid = nhid;
    d->nout = nout;
    for (int j = 0; j < nhid; j++) {
        for (int i = 0; i < d->nin; i++)
            d->w1[j][i] = quant(w1[j * d->nin + i]);
        d->b1[j] = quant(b1[j]);
    }
    for (int k = 0; k < nout; k++) {
        for (int j = 0; j < nhid; j++)
            d->w2[k][j] = quant(w2[k * nhid + j]);
        d->b2[k] = quant(b2[k]);
    }
    for (int i = 0; i < d->nin; i++)
        d->scale[i] = scale[i] ? scale[i] : 1;
    d->loaded = true;
    return 0;
}

/* integer-only forward pass: raw features -> argmax class */
int rmt_dnn_infer(rmt_dnn_t *d, const uint64_t *x)
{
    if (!d || !d->loaded)
        return 0;

    /* normalize: xq[i] = (x[i] / scale[i]) in Q16.16
     * Clamp to Q16.16 representable range so that w*x fits in int64:
     * - xq clamped to ±2^29 (14-bit integer part)
     * - w clamped to ±2^30 (15-bit integer part) by quant()
     * - max product = 2^29 × 2^30 = 2^59
     * - Accumulating nin (<=8) such products: 2^59 × 8 = 2^62, fits int64 */
    int64_t xq[RMT_DNN_MAX_IN];
    for (int i = 0; i < d->nin; i++) {
        uint64_t xi = x[i];
        if (xi > (uint64_t)INT64_MAX >> Q)       /* would overflow */
            xi = (uint64_t)INT64_MAX >> Q;
        int64_t q = (int64_t)((xi << Q) / d->scale[i]);
        /* clamp to Q16.16 range to prevent w*x overflow */
        if (q > (INT64_C(1) << 29))  q =  (INT64_C(1) << 29);
        if (q < -(INT64_C(1) << 29)) q = -(INT64_C(1) << 29);
        xq[i] = q;
    }

    /* hidden layer: ReLU(w1 . x + b1), accumulate in Q32.32.
     * h[] is stored in 64 bits; after the >> Q shift each activation
     * is clamped to ±2^27 so the output layer accumulation (with w2 up
     * to 2^30, nhid up to 32) stays within int64_t range:
     * max acc = 32 × 2^30 × 2^27 = 2^62, safely below INT64_MAX. */
    int64_t h[RMT_DNN_MAX_HID];
    for (int j = 0; j < d->nhid; j++) {
        int64_t acc = (int64_t)d->b1[j] * (INT64_C(1) << Q);
        for (int i = 0; i < d->nin; i++)
            acc += (int64_t)d->w1[j][i] * xq[i];
        int64_t v = acc > 0 ? (acc >> Q) : 0;
        /* clamp to prevent output layer overflow */
        if (v > (INT64_C(1) << 27)) v = (INT64_C(1) << 27);
        h[j] = v;
    }

    /* output layer: linear logits, argmax.
     * best_v starts at INT64_MIN so the comparison is correct for
     * any logit sign, including all-negative logits. */
    int best = 0;
    int64_t best_v = INT64_MIN;
    for (int k = 0; k < d->nout; k++) {
        int64_t acc = (int64_t)d->b2[k] * (INT64_C(1) << Q);
        for (int j = 0; j < d->nhid; j++)
            acc += (int64_t)d->w2[k][j] * h[j];
        if (acc > best_v) {
            best_v = acc;
            best = k;
        }
    }

    atomic_fetch_add_explicit(&d->infers, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&d->class_cnt[best], 1, memory_order_relaxed);
    return best;
}

void rmt_dnn_print_stats(const rmt_dnn_t *d)
{
    printf("  dnn %-6s: %d-%d-%d MLP (Q16.16), %llu inferences [",
           d->name, d->nin, d->nhid, d->nout,
           (unsigned long long)atomic_load_explicit(&d->infers,
                                                    memory_order_relaxed));
    for (int k = 0; k < d->nout; k++)
        printf("%sclass%d=%llu", k ? ", " : "", k,
               (unsigned long long)atomic_load_explicit(&d->class_cnt[k],
                                                        memory_order_relaxed));
    printf("]\n");
}
