/*
 * pipeline.c - RMT pipeline: parser -> N match-action stages -> deparser
 *
 * Every physical stage hosts the tables assigned to it by the P4
 * compiler's control-flow mapping. Applying a table means:
 *   1. gate predicate check (e.g. "if (valid(srh))")
 *   2. tree-engine lookup on the compiled key
 *   3. execute the hit entry's action (or the default action) on the
 *      stage's 16-ALU action engine
 */
#define _POSIX_C_SOURCE 199309L   /* clock_gettime with -std=c11 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rmt.h"

rmt_pipeline_t *rmt_create(void)
{
    rmt_pipeline_t *p = (rmt_pipeline_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->start_state = PS_ACCEPT;
    p->fid_drop = p->fid_eport = -1;
    p->fid_iport = p->fid_ts = -1;
    p->nstages = RMT_MIN_STAGES;
    return p;
}

void rmt_destroy(rmt_pipeline_t *p)
{
    if (!p)
        return;
    for (int i = 0; i < p->ntables; i++)
        rmt_table_free(&p->tables[i]);
    for (int i = 0; i < p->ncaches; i++)
        rmt_cache_destroy(p->caches[i]);
    for (int i = 0; i < p->ndnns; i++)
        rmt_dnn_destroy(p->dnns[i]);
    for (int i = 0; i < p->nsketches; i++)
        rmt_sketch_destroy(p->sketches[i]);
    free(p);
}

int rmt_hdr_add(rmt_pipeline_t *p, const char *name, bool is_meta)
{
    if (p->nhdrs >= RMT_MAX_HDRS)
        return -1;
    hdr_def_t *h = &p->hdefs[p->nhdrs];
    snprintf(h->name, sizeof(h->name), "%s", name);
    h->first_field = p->nfields;
    h->nfields = 0;
    h->bitlen = 0;
    h->is_meta = is_meta;
    return p->nhdrs++;
}

int rmt_field_add(rmt_pipeline_t *p, int hdr, const char *fullname, int bitlen)
{
    if (hdr < 0 || hdr >= p->nhdrs)
        return -1;
    if (p->nfields >= RMT_MAX_FIELDS || bitlen > 128 || bitlen < 0)
        return -1;
    field_def_t *f = &p->fdefs[p->nfields];
    snprintf(f->name, sizeof(f->name), "%s", fullname);
    f->hdr = hdr;
    f->bitlen = (uint16_t)bitlen;
    p->hdefs[hdr].nfields++;
    p->hdefs[hdr].bitlen += bitlen;
    return p->nfields++;
}

int rmt_hdr_id(const rmt_pipeline_t *p, const char *name)
{
    for (int i = 0; i < p->nhdrs; i++)
        if (!strcmp(p->hdefs[i].name, name))
            return i;
    return -1;
}

int rmt_field_id(const rmt_pipeline_t *p, const char *fullname)
{
    for (int i = 0; i < p->nfields; i++)
        if (!strcmp(p->fdefs[i].name, fullname))
            return i;
    return -1;
}

int rmt_arr_id(const rmt_pipeline_t *p, const char *name)
{
    for (int i = 0; i < p->narrs; i++)
        if (!strcmp(p->adefs[i].name, name))
            return i;
    return -1;
}

void rmt_finalize(rmt_pipeline_t *p)
{
    p->fid_drop  = rmt_field_id(p, "meta.drop");
    p->fid_eport = rmt_field_id(p, "meta.egress_port");
    p->fid_iport = rmt_field_id(p, "meta.ingress_port");
    p->fid_ts    = rmt_field_id(p, "meta.timestamp");
    if (p->nstages < RMT_MIN_STAGES)
        p->nstages = RMT_MIN_STAGES;

    /* per-stage table lists for the per-packet fast path */
    memset(p->nstage_tbl, 0, sizeof(p->nstage_tbl));
    for (int t = 0; t < p->ntables; t++) {
        int s = p->tables[t].stage;
        if (s >= 0 && s < RMT_MAX_STAGES &&
            p->nstage_tbl[s] < RMT_MAX_TABLES)
            p->stage_tbl[s][p->nstage_tbl[s]++] = (int8_t)t;
    }

    /* pristine field template: zero values with bitlens preset */
    memset(p->fields_tpl, 0, sizeof(p->fields_tpl));
    for (int i = 0; i < p->nfields; i++)
        p->fields_tpl[i].bitlen = p->fdefs[i].bitlen;

    /* precompute metadata validity mask: these headers are always
     * valid on every packet, avoids per-header loop in phv_reset */
    p->meta_valid_mask = 0;
    for (int h = 0; h < p->nhdrs; h++)
        if (p->hdefs[h].is_meta)
            p->meta_valid_mask |= (1ULL << h);
}

void rmt_phv_reset(const rmt_pipeline_t *p, phv_t *phv)
{
    /* only copy the fields this pipeline defines (avoids touching
     * all 160 slots).  hvalid is a single uint64_t set to the
     * metadata mask (computed once at finalize time).               */
    memcpy(phv->fields, p->fields_tpl,
           (size_t)p->nfields * sizeof(fv_t));
    phv->hvalid = p->meta_valid_mask;   /* metadata always valid    */
    phv->pl = NULL;
    phv->pl_len = 0;
    phv->inject_len = 0;
    phv->has_inject = false;
    memset(phv->dirty, 0, sizeof(phv->dirty));
    phv->structural = false;
}

static inline __attribute__((always_inline))
bool phv_dropped(const rmt_pipeline_t *p, const phv_t *phv)
{
    return p->fid_drop >= 0 &&
           RMT_UNLIKELY(fv_get_u64(&phv->fields[p->fid_drop]) != 0);
}

static inline __attribute__((always_inline))
void apply_table(rmt_pipeline_t * restrict p, rmt_table_t *t, phv_t * restrict phv)
{
    if (t->pred_hdr >= 0 && !phv_hdr_valid(phv, t->pred_hdr)) {
        if (RMT_UNLIKELY(p->verbose))
            printf("    table %-12s skipped (gate: %s not valid)\n",
                   t->name, p->hdefs[t->pred_hdr].name);
        return;
    }

    rmt_entry_t *e = rmt_table_lookup(p, t, phv);
    int action_id;
    const uint64_t *params;

    if (RMT_LIKELY(e != NULL)) {
        action_id = e->action_id;
        params = e->params;
        if (RMT_UNLIKELY(p->verbose))
            printf("    table %-12s HIT  entry#%d -> action %s\n",
                   t->name, e->id, p->actions[action_id].name);
    } else {
        action_id = t->default_action;
        params = t->def_params;
        if (RMT_UNLIKELY(p->verbose))
            printf("    table %-12s MISS      -> %s\n", t->name,
                   action_id >= 0 ? p->actions[action_id].name : "(no-op)");
    }

    if (RMT_LIKELY(action_id >= 0))
        rmt_action_exec(p, &p->actions[action_id], params, phv);
}

/* In-place deparse fast path (report §5.2). For forwarding-style
 * workloads most header bytes are untouched, so instead of
 * re-serializing every field (deparse) and copying the payload
 * separately, copy the whole input packet once and patch back only the
 * fields an action actually rewrote. Valid only when the actions did
 * not reshape the packet (no header add/remove, no cache inject).
 * Returns true when out[] was filled (in_len bytes).                 */
static inline bool try_inplace(const rmt_pipeline_t *p, const phv_t *phv,
                               const uint8_t *in_data, int in_len,
                               uint8_t *out_data)
{
    if (RMT_UNLIKELY(phv->structural || phv->has_inject))
        return false;
    if (in_len < 0) in_len = 0;
    if (in_len > RMT_PKT_MAX) in_len = RMT_PKT_MAX;
    memcpy(out_data, in_data, (size_t)in_len);
    int bitoff = 0;
    for (int h = 0; h < p->nhdrs; h++) {
        const hdr_def_t *hd = &p->hdefs[h];
        if (hd->is_meta || !phv_hdr_valid(phv, h))
            continue;
        for (int i = 0; i < hd->nfields; i++) {
            int fid = hd->first_field + i;
            int blen = p->fdefs[fid].bitlen;
            if (phv->dirty[fid >> 6] & (1ULL << (fid & 63)))
                rmt_field_write(out_data, bitoff, &phv->fields[fid], blen);
            bitoff += blen;
        }
    }
    return true;
}

/* thread-local inject buffer: avoids 2KB stack allocation per-pkt
 * while remaining safe for multi-threaded packet processing.       */
static __thread uint8_t tls_inject_buf[RMT_PKT_MAX];

RMT_HOT
int rmt_process(rmt_pipeline_t * restrict p,
                const rmt_pkt_t * restrict in, rmt_pkt_t * restrict out)
{
    if (RMT_UNLIKELY(!p || !in || !out))
        return -1;

    phv_t phv;
    phv.inject = tls_inject_buf;
    int hdr_bytes = 0;

    out->len = 0;
    out->in_port = in->in_port;
    out->out_port = -1;
    out->dropped = false;

    /* rmt_pkt_t.data[] is exactly RMT_PKT_MAX bytes: reject lengths the
     * caller could not have filled (also catches negative ints).      */
    if (RMT_UNLIKELY((unsigned)in->len > (unsigned)RMT_PKT_MAX))
        return -1;

    rmt_phv_reset(p, &phv);

    /* intrinsic metadata: ingress port + arrival timestamp (ns), the
     * inputs an INT/telemetry dataplane samples per hop */
    if (p->fid_iport >= 0)
        fv_set_u64(&phv.fields[p->fid_iport], (uint64_t)in->in_port);
    if (RMT_UNLIKELY(p->fid_ts >= 0)) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fv_set_u64(&phv.fields[p->fid_ts],
                   (uint64_t)ts.tv_sec * 1000000000ull
                   + (uint64_t)ts.tv_nsec);
    }

    if (RMT_UNLIKELY(p->verbose))
        printf("== ingress: %d bytes on port %d ==\n", in->len, in->in_port);

    if (RMT_UNLIKELY(rmt_parse(p, in->data, in->len, &phv, &hdr_bytes) < 0))
        return -1;

    /* expose the payload to stateful externs (in-network cache) */
    phv.pl = in->data + hdr_bytes;
    phv.pl_len = in->len - hdr_bytes;

    for (int s = 0; s < p->nstages && !phv_dropped(p, &phv); s++) {
        if (RMT_UNLIKELY(p->verbose))
            printf("  -- stage %d --\n", s);
        for (int k = 0; k < p->nstage_tbl[s]; k++) {
            apply_table(p, &p->tables[p->stage_tbl[s][k]], &phv);
            if (phv_dropped(p, &phv))
                break;
        }
    }

    if (phv_dropped(p, &phv)) {
        out->dropped = true;
        if (RMT_UNLIKELY(p->verbose))
            printf("== packet DROPPED ==\n");
        return 0;
    }

    if (try_inplace(p, &phv, in->data, in->len, out->data)) {
        out->len = in->len;
    } else {
        int n = rmt_deparse(p, &phv, out->data, RMT_PKT_MAX);
        if (RMT_UNLIKELY(n < 0))
            return -1;

        /* append payload: cache-injected content (CS hit reply) replaces
         * the original payload; otherwise the bytes the parser did not
         * consume pass through unchanged */
        if (RMT_UNLIKELY(phv.has_inject)) {
            if (phv.inject_len > 0 && n + phv.inject_len <= RMT_PKT_MAX) {
                memcpy(out->data + n, phv.inject, (size_t)phv.inject_len);
                n += phv.inject_len;
            }
        } else {
            int payload = in->len - hdr_bytes;
            if (RMT_LIKELY(payload > 0 && n + payload <= RMT_PKT_MAX)) {
                memcpy(out->data + n, in->data + hdr_bytes, (size_t)payload);
                n += payload;
            }
        }
        out->len = n;
    }
    out->out_port = (p->fid_eport >= 0)
                    ? (int)fv_get_u64(&phv.fields[p->fid_eport]) : -1;
    if (RMT_UNLIKELY(p->verbose))
        printf("== egress: %d bytes -> port %d ==\n", out->len, out->out_port);
    return 0;
}

/* ---- batch processing: amortizes per-packet overhead ---- */

/* shared per-packet body for batch/burst paths; batch_ts is taken
 * once per burst so no per-packet clock_gettime is needed.          */
static inline __attribute__((always_inline))
void process_one(rmt_pipeline_t * restrict p, const rmt_pkt_t * restrict in,
                 rmt_pkt_t * restrict out, uint64_t batch_ts)
{
    out->len = 0;
    out->in_port = 0;
    out->out_port = -1;
    out->dropped = false;
    if (RMT_UNLIKELY(!in)) {
        out->dropped = true;
        return;
    }
    out->in_port = in->in_port;
    if (RMT_UNLIKELY((unsigned)in->len > (unsigned)RMT_PKT_MAX)) {
        out->dropped = true;
        return;
    }

    phv_t phv;
    phv.inject = tls_inject_buf;
    int hdr_bytes = 0;

    rmt_phv_reset(p, &phv);

    if (p->fid_iport >= 0)
        fv_set_u64(&phv.fields[p->fid_iport], (uint64_t)in->in_port);
    if (p->fid_ts >= 0)
        fv_set_u64(&phv.fields[p->fid_ts], batch_ts);

    if (RMT_UNLIKELY(rmt_parse(p, in->data, in->len,
                                &phv, &hdr_bytes) < 0)) {
        out->dropped = true;
        return;
    }

    phv.pl = in->data + hdr_bytes;
    phv.pl_len = in->len - hdr_bytes;

    for (int s = 0; s < p->nstages && !phv_dropped(p, &phv); s++) {
        for (int k = 0; k < p->nstage_tbl[s]; k++) {
            apply_table(p, &p->tables[p->stage_tbl[s][k]], &phv);
            if (phv_dropped(p, &phv))
                break;
        }
    }

    if (phv_dropped(p, &phv)) {
        out->dropped = true;
        return;
    }

    if (try_inplace(p, &phv, in->data, in->len, out->data)) {
        out->len = in->len;
        out->out_port = (p->fid_eport >= 0)
                        ? (int)fv_get_u64(&phv.fields[p->fid_eport]) : -1;
        return;
    }

    int n = rmt_deparse(p, &phv, out->data, RMT_PKT_MAX);
    if (RMT_UNLIKELY(n < 0)) {
        out->dropped = true;
        return;
    }

    if (RMT_UNLIKELY(phv.has_inject)) {
        if (phv.inject_len > 0 && n + phv.inject_len <= RMT_PKT_MAX) {
            memcpy(out->data + n, phv.inject, (size_t)phv.inject_len);
            n += phv.inject_len;
        }
    } else {
        int payload = in->len - hdr_bytes;
        if (RMT_LIKELY(payload > 0 && n + payload <= RMT_PKT_MAX)) {
            memcpy(out->data + n, in->data + hdr_bytes,
                   (size_t)payload);
            n += payload;
        }
    }
    out->len = n;
    out->out_port = (p->fid_eport >= 0)
                    ? (int)fv_get_u64(&phv.fields[p->fid_eport]) : -1;
}

static inline uint64_t batch_timestamp(const rmt_pipeline_t *p)
{
    if (p->fid_ts < 0)
        return 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

RMT_HOT
int rmt_process_batch(rmt_pipeline_t * restrict p,
                      const rmt_pkt_t * restrict in,
                      rmt_pkt_t * restrict out, int count)
{
    if (count <= 0) return 0;
    if (!p || !in || !out) return -1;
    if (count > RMT_BATCH_MAX)
        count = RMT_BATCH_MAX;

    /* single timestamp for the entire batch (avoids per-pkt syscall) */
    uint64_t batch_ts = batch_timestamp(p);

    for (int i = 0; i < count; i++)
        process_one(p, &in[i], &out[i], batch_ts);
    return 0;
}

/* pointer-array burst: packets arrive as pointers straight out of a
 * lock-free ring (mq.c), no copy into a contiguous array needed.    */
RMT_HOT
int rmt_process_burst(rmt_pipeline_t * restrict p,
                      rmt_pkt_t * const * restrict in,
                      rmt_pkt_t * restrict out, int count)
{
    if (count <= 0) return 0;
    if (!p || !in || !out) return -1;
    if (count > RMT_BATCH_MAX)
        count = RMT_BATCH_MAX;

    uint64_t batch_ts = batch_timestamp(p);

    for (int i = 0; i < count; i++)
        process_one(p, in[i], &out[i], batch_ts);
    return 0;
}

