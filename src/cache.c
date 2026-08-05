/*
 * cache.c - in-network cache engine (stateful extern)
 *
 * A dataplane-writable exact-match store keyed by a 128-bit field
 * value (e.g. an NDN name). Unlike the match-action tables (written
 * only by the control plane), this memory is read AND written by the
 * action engine at packet rate, which is what an in-network cache
 * needs:
 *
 *   NDN Content Store (CS): name -> content bytes   (get/put)
 *   Pending Interest Table (PIT): name -> port      (put-if-absent/take)
 *   Utilization table (telescope): addr -> epoch    (mark/probe + aging)
 *
 * Implementation: open-addressing hash (FNV-1a over the key), linear
 * probing over a small neighborhood, oldest-entry eviction when the
 * neighborhood is full. A mutex per cache keeps concurrent packet
 * processing safe (the rest of the pipeline is read-only/state-free).
 *
 * The utilization view reuses the slot store: rmt_cache_mark() stamps
 * the key with the current epoch (in aux), rmt_cache_probe() treats
 * an entry older than ttl_epochs as expired (lazily freeing it). The
 * epoch is advanced only by the control plane (rmt_cache_tick), which
 * models MORP4's configurable time granularity.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "rmt.h"

/* ---- fast spinlock for short critical sections (<200ns hold) ---- */
typedef atomic_flag spin_t;
#define SPIN_INIT ATOMIC_FLAG_INIT
static inline void spin_lock(spin_t *s)
{
    while (atomic_flag_test_and_set_explicit(s, memory_order_acquire)) {
#ifdef __x86_64__
        __builtin_ia32_pause();
#endif
    }
}
static inline void spin_unlock(spin_t *s)
{
    atomic_flag_clear_explicit(s, memory_order_release);
}

#define CACHE_PROBE   8              /* linear probe neighborhood     */
#define CACHE_DATA    1024           /* max content bytes per entry   */

typedef struct {
    bool     used;
    uint8_t  key[16];
    uint8_t  data[CACHE_DATA];
    int      len;
    uint64_t aux;                    /* PIT: ingress port             */
    uint64_t seq;                    /* insertion order (eviction)    */
} cache_slot_t;

struct rmt_cache {
    char            name[RMT_NAME_LEN];
    int             nslots;
    cache_slot_t   *slots;
    uint64_t        seq;
    spin_t          lock;            /* fast spinlock (was mutex)     */
    /* utilization aging (telescope) */
    uint64_t        epoch;           /* control-plane time            */
    uint64_t        ttl;             /* epochs an entry stays live    */
    /* stats (atomic: read without lock from print_stats) */
    _Atomic uint64_t hits, misses, inserts, evictions;
} RMT_ALIGNED(64);                   /* cache-line aligned            */

rmt_cache_t *rmt_cache_create(const char *name, int nslots)
{
    rmt_cache_t *c = (rmt_cache_t *)calloc(1, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->nslots = nslots > 0 ? nslots : 64;
    c->slots = (cache_slot_t *)calloc((size_t)c->nslots, sizeof(*c->slots));
    c->ttl = UINT64_MAX;                 /* aging off by default      */
    /* lock is zero-initialized by calloc (ATOMIC_FLAG_INIT) */
    return c;
}

void rmt_cache_destroy(rmt_cache_t *c)
{
    if (!c)
        return;
    free(c->slots);
    free(c);
}

const char *rmt_cache_name(const rmt_cache_t *c)
{
    return c->name;
}

static uint32_t fnv1a(const uint8_t *k)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) {
        h ^= k[i];
        h *= 16777619u;
    }
    return h;
}

/* find slot holding key, or NULL (mu held) */
static cache_slot_t *find(rmt_cache_t *c, const uint8_t *k)
{
    uint32_t h = fnv1a(k) % (uint32_t)c->nslots;
    for (int i = 0; i < CACHE_PROBE; i++) {
        cache_slot_t *s = &c->slots[(h + (uint32_t)i) % (uint32_t)c->nslots];
        if (s->used && !memcmp(s->key, k, 16))
            return s;
    }
    return NULL;
}

/* free (or oldest, evicting) slot in the neighborhood (mu held) */
static cache_slot_t *alloc_slot(rmt_cache_t *c, const uint8_t *k)
{
    uint32_t h = fnv1a(k) % (uint32_t)c->nslots;
    cache_slot_t *victim = NULL;
    for (int i = 0; i < CACHE_PROBE; i++) {
        cache_slot_t *s = &c->slots[(h + (uint32_t)i) % (uint32_t)c->nslots];
        if (!s->used)
            return s;
        if (!victim || s->seq < victim->seq)
            victim = s;
    }
    c->evictions++;
    victim->used = false;
    return victim;
}

bool rmt_cache_get(rmt_cache_t *c, const fv_t *key,
                   uint8_t *data, int cap, int *len, uint64_t *aux)
{
    spin_lock(&c->lock);
    cache_slot_t *s = find(c, key->v);
    if (RMT_UNLIKELY(!s)) {
        c->misses++;
        spin_unlock(&c->lock);
        return false;
    }
    c->hits++;
    int n = s->len < cap ? s->len : cap;
    if (data && n > 0)
        memcpy(data, s->data, (size_t)n);
    if (len)
        *len = n;
    if (aux)
        *aux = s->aux;
    spin_unlock(&c->lock);
    return true;
}

bool rmt_cache_put(rmt_cache_t *c, const fv_t *key,
                   const uint8_t *data, int len, uint64_t aux,
                   bool only_if_absent)
{
    if (len > CACHE_DATA)
        len = CACHE_DATA;
    spin_lock(&c->lock);
    cache_slot_t *s = find(c, key->v);
    if (s && only_if_absent) {
        spin_unlock(&c->lock);
        return false;
    }
    if (!s) {
        s = alloc_slot(c, key->v);
        memcpy(s->key, key->v, 16);
        s->used = true;
    }
    if (data && len > 0)
        memcpy(s->data, data, (size_t)len);
    s->len = len > 0 ? len : 0;
    s->aux = aux;
    s->seq = ++c->seq;
    c->inserts++;
    spin_unlock(&c->lock);
    return true;
}

bool rmt_cache_take(rmt_cache_t *c, const fv_t *key, uint64_t *aux)
{
    spin_lock(&c->lock);
    cache_slot_t *s = find(c, key->v);
    if (RMT_UNLIKELY(!s)) {
        c->misses++;
        spin_unlock(&c->lock);
        return false;
    }
    c->hits++;
    if (aux)
        *aux = s->aux;
    s->used = false;
    spin_unlock(&c->lock);
    return true;
}

/* ---------------- utilization view (dynamic telescope) ----------- */

void rmt_cache_set_aging(rmt_cache_t *c, uint64_t ttl_epochs)
{
    spin_lock(&c->lock);
    c->ttl = ttl_epochs;
    spin_unlock(&c->lock);
}

uint64_t rmt_cache_tick(rmt_cache_t *c)
{
    spin_lock(&c->lock);
    uint64_t e = ++c->epoch;
    spin_unlock(&c->lock);
    return e;
}

/* entry live iff marked within the last ttl epochs (mu held) */
static bool slot_live(const rmt_cache_t *c, const cache_slot_t *s)
{
    return c->epoch - s->aux <= c->ttl;
}

/* mark key as used at the current epoch (insert or refresh) */
void rmt_cache_mark(rmt_cache_t *c, const fv_t *key)
{
    spin_lock(&c->lock);
    cache_slot_t *s = find(c, key->v);
    if (!s) {
        s = alloc_slot(c, key->v);
        memcpy(s->key, key->v, 16);
        s->used = true;
        s->len = 0;
        c->inserts++;
    }
    s->aux = c->epoch;                   /* utilization timestamp     */
    s->seq = ++c->seq;
    spin_unlock(&c->lock);
}

/* non-destructive liveness probe; expired entries are lazily freed */
bool rmt_cache_probe(rmt_cache_t *c, const fv_t *key)
{
    spin_lock(&c->lock);
    cache_slot_t *s = find(c, key->v);
    if (s && !slot_live(c, s)) {         /* aged out -> unused again  */
        s->used = false;
        s = NULL;
    }
    if (s)
        c->hits++;
    else
        c->misses++;
    spin_unlock(&c->lock);
    return s != NULL;
}

int rmt_cache_live(rmt_cache_t *c)
{
    int n = 0;
    spin_lock(&c->lock);
    for (int i = 0; i < c->nslots; i++)
        if (c->slots[i].used && slot_live(c, &c->slots[i]))
            n++;
    spin_unlock(&c->lock);
    return n;
}

void rmt_cache_print_stats(const rmt_cache_t *c)
{
    int used = 0;
    spin_lock((spin_t *)&c->lock);
    for (int i = 0; i < c->nslots; i++)
        if (c->slots[i].used)
            used++;
    spin_unlock((spin_t *)&c->lock);
    printf("  cache %-6s: %d/%d entries, %llu hits, %llu misses, "
           "%llu inserts, %llu evictions\n",
           c->name, used, c->nslots,
           (unsigned long long)atomic_load(&c->hits),
           (unsigned long long)atomic_load(&c->misses),
           (unsigned long long)atomic_load(&c->inserts),
           (unsigned long long)atomic_load(&c->evictions));
}
