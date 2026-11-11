/*
 * sketch.c - Count-Min Sketch extern for flow frequency estimation
 *
 * A probabilistic counter array that estimates flow frequencies with
 * bounded error. The sketch maintains d × w atomic counters; update
 * hashes the flow key d times (one per row, with different seeds) and
 * atomically increments each indexed counter. Query returns the
 * minimum of the d counter values (standard CMS estimator, never
 * underestimates).
 *
 * Error bound: ε = e/w with probability 1 - (1/2)^d.
 * Recommended defaults: d=4, w=1024 → 16 KB, ε ≈ 0.27%, 93.75% conf.
 *
 * Thread-safety: updates use relaxed atomic increments (no locks);
 * queries use relaxed reads. Multiple workers can update concurrently.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "rmt.h"

/* ---- FNV-1a hash with seed mixing ---- */
static inline uint32_t sketch_hash(const uint8_t *key, int len, int seed)
{
    uint32_t h = 2166136261u ^ (uint32_t)(seed * 2654435761u);
    for (int i = 0; i < len; i++) {
        h ^= key[i];
        h *= 16777619u;
    }
    /* extra mixing to decorrelate rows */
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    return h;
}

/* ---- extract flow key from PHV ---- */
/* Concatenates the byte values of the bound key fields into a compact
 * buffer. Each field is truncated/padded to its declared bitlen.      */
static void extract_key(const rmt_sketch_t *s, const phv_t *phv,
                        uint8_t *out, int *out_len)
{
    int byte_off = 0;
    for (int i = 0; i < s->nkeys; i++) {
        const fv_t *f = &phv->fields[s->key_fids[i]];
        if (f->bitlen == 0) continue;    /* skip uninitialized fields */
        int bytes = (f->bitlen + 7) / 8;
        if (bytes > 16) bytes = 16;      /* clamp: fv_t is 16 bytes */
        if (byte_off + bytes > 32) break;   /* hard cap: 32 bytes */
        /* copy low bytes (right-aligned big-endian in fv_t.v) */
        memcpy(out + byte_off, f->v + (16 - bytes), (size_t)bytes);
        byte_off += bytes;
    }
    *out_len = byte_off;
}

/* ---- public API ---- */

rmt_sketch_t *rmt_sketch_create(const char *name, int d, int w,
                                 int nkeys, const int *key_fids)
{
    if (d < 1 || d > RMT_SKETCH_MAX_D) return NULL;
    if (w < 1 || w > RMT_SKETCH_MAX_W) return NULL;
    if (nkeys < 1 || nkeys > RMT_DNN_MAX_IN) return NULL;

    rmt_sketch_t *s = (rmt_sketch_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    snprintf(s->name, sizeof(s->name), "%s", name);
    s->d = d;
    s->w = w;
    s->nkeys = nkeys;
    for (int i = 0; i < nkeys; i++)
        s->key_fids[i] = key_fids[i];

    size_t total = (size_t)d * (size_t)w;
    s->counters = (uint32_t *)calloc(total, sizeof(uint32_t));
    if (!s->counters) { free(s); return NULL; }

    atomic_store(&s->updates, 0);
    atomic_store(&s->queries, 0);
    return s;
}

void rmt_sketch_destroy(rmt_sketch_t *s)
{
    if (!s) return;
    free(s->counters);
    free(s);
}

/* sketch_inc: atomically increment d counters (one per row) */
void rmt_sketch_inc(rmt_sketch_t *s, const phv_t *phv)
{
    uint8_t key[32];
    int key_len;
    extract_key(s, phv, key, &key_len);
    if (key_len == 0) return;

    for (int i = 0; i < s->d; i++) {
        uint32_t idx = sketch_hash(key, key_len, i) % (uint32_t)s->w;
        __atomic_add_fetch(&s->counters[(size_t)i * s->w + idx], 1,
                           __ATOMIC_RELAXED);
    }
    atomic_fetch_add(&s->updates, 1);
}

/* sketch_get: return min of d counter values (CMS estimator) */
uint32_t rmt_sketch_get(rmt_sketch_t *s, const phv_t *phv)
{
    uint8_t key[32];
    int key_len;
    extract_key(s, phv, key, &key_len);
    if (key_len == 0) return 0;

    uint32_t min_val = UINT32_MAX;
    for (int i = 0; i < s->d; i++) {
        uint32_t idx = sketch_hash(key, key_len, i) % (uint32_t)s->w;
        uint32_t v = __atomic_load_n(&s->counters[(size_t)i * s->w + idx],
                                      __ATOMIC_RELAXED);
        if (v < min_val) min_val = v;
    }
    atomic_fetch_add(&s->queries, 1);
    return min_val;
}

void rmt_sketch_reset(rmt_sketch_t *s)
{
    if (!s) return;
    /* Use atomic stores to avoid data race with concurrent
     * rmt_sketch_inc() (which uses atomic_add_fetch).  Note: this is
     * still non-atomic at the sketch level — callers must ensure no
     * concurrent inc/get during reset (e.g. stop workers first). */
    size_t total = (size_t)s->d * (size_t)s->w;
    for (size_t i = 0; i < total; i++)
        __atomic_store_n(&s->counters[i], 0, __ATOMIC_RELAXED);
    atomic_store(&s->updates, 0);
    atomic_store(&s->queries, 0);
}

const char *rmt_sketch_name(const rmt_sketch_t *s)
{
    return s ? s->name : "(null)";
}

void rmt_sketch_print_stats(const rmt_sketch_t *s)
{
    if (!s) return;
    printf("  sketch %-8s: d=%d w=%d keys=%d  %llu updates  %llu queries\n",
           s->name, s->d, s->w, s->nkeys,
           (unsigned long long)atomic_load(&s->updates),
           (unsigned long long)atomic_load(&s->queries));
}
