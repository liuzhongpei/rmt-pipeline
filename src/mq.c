/*
 * mq.c - multi-queue parallel packet processing (RSS model)
 *
 * Emulates a multi-queue NIC + per-core run-to-completion workers,
 * the same scaling model DPDK / AF_XDP applications use:
 *
 *   dispatcher --RSS hash--> [SPSC ring 0] --> worker 0 (core 0)
 *                            [SPSC ring 1] --> worker 1 (core 1)
 *                            ...
 *                            [SPSC ring N] --> worker N (core N)
 *
 * Design points:
 *   - one lock-free single-producer/single-consumer ring per worker:
 *     the dispatcher is the only producer and the worker the only
 *     consumer, so head/tail need no CAS, just acquire/release
 *   - head and tail live on separate cache lines (no false sharing
 *     between producer and consumer cores)
 *   - the ring carries packet POINTERS (8 bytes/slot), not packet
 *     copies: zero-copy hand-off, ~2KB memcpy per packet avoided
 *   - RSS invariant: same flow -> same hash -> same queue, so
 *     per-flow packet order is preserved without any locking
 *   - workers batch-dequeue up to RMT_BATCH_MAX packets and process
 *     them via rmt_process_burst (one timestamp per burst), then
 *     invoke the user callback in worker context (models TX)
 *   - workers are pinned to distinct cores (pthread_setaffinity_np)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "rmt.h"

#define MQ_MAX_WORKERS 128

/* spins before falling back to sched_yield(): keeps latency low on
 * dedicated cores yet avoids starving other threads when cores are
 * oversubscribed (workers == cores + dispatcher, busy host, ...)    */
#define MQ_SPIN_LIMIT  256

/* same PAUSE hint the cache spinlock uses */
#if defined(__x86_64__) || defined(__i386__)
#define cpu_relax() __builtin_ia32_pause()
#else
#define cpu_relax() ((void)0)
#endif

/* ---------------- lock-free SPSC ring ---------------- */
typedef struct {
    _Atomic uint32_t head;                /* producer-only writes    */
    char _pad0[64 - sizeof(_Atomic uint32_t)];
    _Atomic uint32_t tail;                /* consumer-only writes    */
    char _pad1[64 - sizeof(_Atomic uint32_t)];
    uint32_t    mask;                     /* slots - 1 (power of 2)  */
    rmt_pkt_t **slot;
} RMT_ALIGNED(64) spsc_t;

static inline bool ring_push(spsc_t *r, rmt_pkt_t *pkt)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t > r->mask)                  /* full */
        return false;
    r->slot[h & r->mask] = pkt;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    return true;
}

static inline int ring_pop_burst(spsc_t *r, rmt_pkt_t **out, int max)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    int n = (int)(h - t);
    if (n <= 0)
        return 0;
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = r->slot[(t + (uint32_t)i) & r->mask];
    atomic_store_explicit(&r->tail, t + (uint32_t)n, memory_order_release);
    return n;
}

/* ---------------- worker / mq ---------------- */
typedef struct {
    struct rmt_mq   *mq;
    spsc_t           ring;
    pthread_t        tid;
    int              id;
    rmt_pkt_t       *out;                 /* RMT_BATCH_MAX results   */
    uint64_t         submitted;           /* dispatcher-side only (single writer, no atomics needed) */
    _Atomic uint64_t processed;           /* worker-side, for flush  */
    uint64_t         dropped;
} RMT_ALIGNED(64) mq_worker_t;

struct rmt_mq {
    rmt_pipeline_t *pipe;
    int             nworkers;
    rmt_mq_cb_t     cb;
    void           *cb_arg;
    _Atomic bool    running;
    mq_worker_t    *w;
};

uint32_t rmt_rss_hash(const uint8_t *key, int len)
{
    uint32_t h = 2166136261u;             /* FNV-1a 32-bit           */
    for (int i = 0; i < len; i++) {
        h ^= key[i];
        h *= 16777619u;
    }
    return h;
}

static void *mq_worker_main(void *v)
{
    mq_worker_t *w = v;
    struct rmt_mq *mq = w->mq;
    rmt_pkt_t *burst[RMT_BATCH_MAX];
    int idle = 0;

#ifdef __linux__
    /* pin to a dedicated core: stable caches, no migration */
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET((unsigned)w->id % (unsigned)ncpu, &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
    }
#endif

    for (;;) {
        int n = ring_pop_burst(&w->ring, burst, RMT_BATCH_MAX);
        if (n == 0) {
            /* ring drained: exit only after stop was requested */
            if (!atomic_load_explicit(&mq->running, memory_order_acquire))
                break;
            /* adaptive idle: spin briefly, then yield the core so
             * the dispatcher can run even when cores are oversubscribed */
            if (++idle > MQ_SPIN_LIMIT)
                sched_yield();
            else
                cpu_relax();
            continue;
        }
        idle = 0;

        rmt_process_burst(mq->pipe, burst, w->out, n);

        for (int i = 0; i < n; i++) {
            if (w->out[i].dropped)
                w->dropped++;
            if (mq->cb)
                mq->cb(w->id, burst[i], &w->out[i], mq->cb_arg);
        }
        atomic_fetch_add_explicit(&w->processed, (uint64_t)n,
                                  memory_order_release);
    }
    return NULL;
}

rmt_mq_t *rmt_mq_create(rmt_pipeline_t *p, int nworkers, int ring_order,
                        rmt_mq_cb_t cb, void *arg)
{
    if (!p || nworkers < 1)
        return NULL;
    if (nworkers > MQ_MAX_WORKERS)
        nworkers = MQ_MAX_WORKERS;
    if (ring_order < 6)   ring_order = 6;    /* >= one full burst    */
    if (ring_order > 20)  ring_order = 20;

    rmt_mq_t *mq = calloc(1, sizeof(*mq));
    if (!mq)
        return NULL;
    mq->pipe = p;
    mq->nworkers = nworkers;
    mq->cb = cb;
    mq->cb_arg = arg;

    mq->w = aligned_alloc(64, (size_t)nworkers * sizeof(mq_worker_t));
    if (!mq->w) {
        free(mq);
        return NULL;
    }
    memset(mq->w, 0, (size_t)nworkers * sizeof(mq_worker_t));

    uint32_t slots = 1u << ring_order;
    for (int i = 0; i < nworkers; i++) {
        mq_worker_t *w = &mq->w[i];
        w->mq = mq;
        w->id = i;
        w->ring.mask = slots - 1;
        w->ring.slot = calloc(slots, sizeof(rmt_pkt_t *));
        if (!w->ring.slot) {
            rmt_mq_destroy(mq);
            return NULL;
        }
        w->out = calloc(RMT_BATCH_MAX, sizeof(rmt_pkt_t));
        if (!w->out) {
            rmt_mq_destroy(mq);
            return NULL;
        }
    }
    return mq;
}

int rmt_mq_start(rmt_mq_t *mq)
{
    atomic_store(&mq->running, true);
    for (int i = 0; i < mq->nworkers; i++) {
        if (pthread_create(&mq->w[i].tid, NULL,
                           mq_worker_main, &mq->w[i]) != 0) {
            atomic_store(&mq->running, false);
            for (int j = 0; j < i; j++)
                pthread_join(mq->w[j].tid, NULL);
            return -1;
        }
    }
    return 0;
}

/* dispatch by RSS hash; spins (with backpressure) if the target
 * queue is full -- packets of one flow are never reordered.         */
bool rmt_mq_submit(rmt_mq_t *mq, rmt_pkt_t *pkt, uint32_t hash)
{
    if (RMT_UNLIKELY(!mq || !pkt))
        return false;
    /* check running BEFORE attempting to push to avoid race during shutdown */
    if (!atomic_load_explicit(&mq->running, memory_order_acquire))
        return false;

    mq_worker_t *w = &mq->w[hash % (uint32_t)mq->nworkers];
    int spins = 0;
    while (!ring_push(&w->ring, pkt)) {
        if (!atomic_load_explicit(&mq->running, memory_order_acquire))
            return false;
        if (++spins > MQ_SPIN_LIMIT) {      /* let the consumer run  */
            sched_yield();
            spins = 0;
        } else {
            cpu_relax();
        }
    }
    w->submitted++;
    return true;
}

/* wait until every submitted packet has been fully processed
 * (callback included), not merely dequeued                          */
void rmt_mq_flush(rmt_mq_t *mq)
{
    for (int i = 0; i < mq->nworkers; i++) {
        mq_worker_t *w = &mq->w[i];
        int spins = 0;
        while (atomic_load_explicit(&w->processed, memory_order_acquire)
               < w->submitted) {
            if (++spins > MQ_SPIN_LIMIT) {
                sched_yield();
                spins = 0;
            } else {
                cpu_relax();
            }
        }
    }
}

void rmt_mq_stop(rmt_mq_t *mq)
{
    if (!atomic_load(&mq->running))
        return;
    atomic_store_explicit(&mq->running, false, memory_order_release);
    for (int i = 0; i < mq->nworkers; i++)
        pthread_join(mq->w[i].tid, NULL);   /* workers drain first   */
}

void rmt_mq_destroy(rmt_mq_t *mq)
{
    if (!mq)
        return;
    rmt_mq_stop(mq);
    if (mq->w) {
        for (int i = 0; i < mq->nworkers; i++) {
            free(mq->w[i].ring.slot);
            free(mq->w[i].out);
        }
        free(mq->w);
    }
    free(mq);
}

uint64_t rmt_mq_worker_processed(const rmt_mq_t *mq, int worker)
{
    if (!mq || worker < 0 || worker >= mq->nworkers)
        return 0;
    return atomic_load(&mq->w[worker].processed);
}

uint64_t rmt_mq_worker_dropped(const rmt_mq_t *mq, int worker)
{
    if (!mq || worker < 0 || worker >= mq->nworkers)
        return 0;
    return mq->w[worker].dropped;
}

/* ---------------- 5-tuple flow classification ---------------- */
/*
 * rmt_rss_flow_hash: parse Ethernet / IPv4 / IPv6 (walking common
 * extension headers, incl. SRH) down to TCP/UDP and hash the
 * 5-tuple {src addr, dst addr, proto, sport, dport}.  All packets
 * of one flow always land on the same queue; unknown or truncated
 * packets fall back to hashing the raw bytes, fragments (non-first)
 * fall back to the 3-tuple so they still stay per-flow-pair stable.
 */
uint32_t rmt_rss_flow_hash(const uint8_t *pkt, int len)
{
    uint32_t h = 2166136261u;                 /* FNV-1a running hash */
#define MIX(b)  do { h ^= (b); h *= 16777619u; } while (0)
#define MIXN(p, n) do { for (int _i = 0; _i < (n); _i++) MIX((p)[_i]); } while (0)
/* murmur3 finalizer: FNV-1a low bits diffuse poorly, and the queue
 * index is hash %% nqueues -- without avalanching, structured tuples
 * (correlated dst/ports) collapse onto few queues.                  */
#define FMIX() do {                             \
        h ^= h >> 16; h *= 0x85ebca6bu;         \
        h ^= h >> 13; h *= 0xc2b2ae35u;         \
        h ^= h >> 16;                           \
    } while (0)

    if (len < 14)
        goto raw;

    uint16_t etype = (uint16_t)(pkt[12] << 8 | pkt[13]);
    int l3 = 14;
    if (etype == 0x8100 && len >= 18) {       /* single VLAN tag     */
        etype = (uint16_t)(pkt[16] << 8 | pkt[17]);
        l3 = 18;
    }

    if (etype == 0x0800) {                    /* ---- IPv4 ----      */
        if (len < l3 + 20)
            goto raw;
        const uint8_t *ip = pkt + l3;
        int ihl = (ip[0] & 0x0f) * 4;
        if (ihl < 20 || len < l3 + ihl)
            goto raw;
        uint8_t proto = ip[9];
        MIXN(ip + 12, 8);                     /* src + dst           */
        MIX(proto);
        bool frag = (ip[6] & 0x1f) != 0 || ip[7] != 0;  /* offset>0  */
        if (!frag && (proto == 6 || proto == 17) &&
            len >= l3 + ihl + 4)
            MIXN(ip + ihl, 4);                /* sport + dport       */
        FMIX();
        return h;
    }

    if (etype == 0x86dd) {                    /* ---- IPv6 ----      */
        if (len < l3 + 40)
            goto raw;
        const uint8_t *ip = pkt + l3;
        MIXN(ip + 8, 32);                     /* src + dst           */
        uint8_t nh = ip[6];
        int off = l3 + 40;
        /* walk hop-by-hop(0) / routing incl. SRH(43) / dst-opts(60) */
        for (int guard = 0; guard < 8; guard++) {
            if (nh == 6 || nh == 17) {        /* TCP / UDP           */
                MIX(nh);
                if (len >= off + 4)
                    MIXN(pkt + off, 4);       /* sport + dport       */
                FMIX();
                return h;
            }
            if (nh == 0 || nh == 43 || nh == 60) {
                if (len < off + 2)
                    break;
                uint8_t next = pkt[off];
                int ehl = (pkt[off + 1] + 1) * 8;
                off += ehl;
                nh = next;
                continue;
            }
            break;                            /* frag/ESP/unknown    */
        }
        MIX(nh);                              /* 3-tuple fallback    */
        FMIX();
        return h;
    }

raw:                                          /* non-IP: raw bytes   */
    MIXN(pkt, len < 34 ? len : 34);
    FMIX();
    return h;
#undef FMIX
#undef MIXN
#undef MIX
}
