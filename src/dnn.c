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
 * threads may infer concurrently; only the statistics counters are
 * protected by a mutex.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
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

    pthread_mutex_t mu;                  /* stats only                */
    uint64_t infers;
    uint64_t class_cnt[RMT_DNN_MAX_OUT];
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
    pthread_mutex_init(&d->mu, NULL);
    return d;
}

void rmt_dnn_destroy(rmt_dnn_t *d)
{
    if (!d)
        return;
    pthread_mutex_destroy(&d->mu);
    free(d);
}

const char *rmt_dnn_name(const rmt_dnn_t *d)       { return d->name; }
int rmt_dnn_num_inputs(const rmt_dnn_t *d)         { return d->nin; }
int rmt_dnn_input_fid(const rmt_dnn_t *d, int i)   { return d->in_fid[i]; }

static int32_t quant(float x)
{
    float q = x * (float)(1 << Q);
    if (q > 2147483000.0f)  q = 2147483000.0f;
    if (q < -2147483000.0f) q = -2147483000.0f;
    return (int32_t)(q >= 0 ? q + 0.5f : q - 0.5f);
}

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
     * Saturate large inputs to prevent overflow in the shift. */
    int64_t xq[RMT_DNN_MAX_IN];
    for (int i = 0; i < d->nin; i++) {
        uint64_t xi = x[i];
        if (xi > (uint64_t)INT64_MAX >> Q)       /* would overflow */
            xi = (uint64_t)INT64_MAX >> Q;
        xq[i] = (int64_t)((xi << Q) / d->scale[i]);
    }

    /* hidden layer: ReLU(w1 . x + b1), accumulate in Q32.32.
     * h[] is stored in 64 bits; after the >> Q shift each activation
     * lies inside the Q16.16 range, so the wider storage is
     * value-identical to a 32-bit result. */
    int64_t h[RMT_DNN_MAX_HID];
    for (int j = 0; j < d->nhid; j++) {
        int64_t acc = (int64_t)d->b1[j] << Q;
        for (int i = 0; i < d->nin; i++)
            acc += (int64_t)d->w1[j][i] * xq[i];
        h[j] = acc > 0 ? (acc >> Q) : 0;       /* back to Q16.16 */
    }

    /* output layer: linear logits, argmax.
     * best_v starts at INT64_MIN so the comparison is correct for
     * any logit sign, including all-negative logits. */
    int best = 0;
    int64_t best_v = INT64_MIN;
    for (int k = 0; k < d->nout; k++) {
        int64_t acc = (int64_t)d->b2[k] << Q;
        for (int j = 0; j < d->nhid; j++)
            acc += (int64_t)d->w2[k][j] * h[j];
        if (acc > best_v) {
            best_v = acc;
            best = k;
        }
    }

    pthread_mutex_lock(&d->mu);
    d->infers++;
    d->class_cnt[best]++;
    pthread_mutex_unlock(&d->mu);
    return best;
}

void rmt_dnn_print_stats(const rmt_dnn_t *d)
{
    pthread_mutex_lock((pthread_mutex_t *)&d->mu);
    printf("  dnn %-6s: %d-%d-%d MLP (Q16.16), %llu inferences [",
           d->name, d->nin, d->nhid, d->nout,
           (unsigned long long)d->infers);
    for (int k = 0; k < d->nout; k++)
        printf("%sclass%d=%llu", k ? ", " : "", k,
               (unsigned long long)d->class_cnt[k]);
    pthread_mutex_unlock((pthread_mutex_t *)&d->mu);
    printf("]\n");
}
