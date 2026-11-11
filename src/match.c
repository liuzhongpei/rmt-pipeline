/*
 * match.c - path-compressed (radix) lookup engine
 *
 * Each match table's key is a list of fields (e.g. the 5-tuple:
 * src addr, dst addr, protocol, sport, dport). The lookup structure is
 * a chain of radix (Patricia) tries, one trie per key field:
 *
 *   field0 trie --(terminal node)--> field1 trie --> ... --> entries
 *
 * A key field supports EXACT (prefix == full width) and LPM matching
 * (prefix length 0 acts as wildcard). Unlike a naive one-bit-per-level
 * trie, each edge carries a compressed label (a run of key bits), so
 * single-child chains collapse into one node: a /128 IPv6 lookup visits
 * only the few branching/attachment nodes instead of 128 levels. This
 * cuts the serialized dependent-load depth dramatically for LPM.
 *
 * Lookup walks each trie along the packet's key bits, remembering every
 * node that carries an attachment, and backtracks longest-prefix-first
 * across fields, so the most specific combined rule wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rmt.h"

/* ------------------------------------------------------------------ */
/* Node arena: trie nodes are allocated from per-table blocks instead  */
/* of individual calloc() calls. Nodes created during an insert walk   */
/* land in consecutive slots, so a lookup that follows the same path   */
/* streams through adjacent memory (prefetch-friendly, fewer cache     */
/* misses) instead of chasing scattered heap objects. Blocks are       */
/* calloc'ed once, so every node starts zeroed and is handed out once. */
/* ------------------------------------------------------------------ */
#define RMT_ARENA_NODES 1024
typedef struct rmt_arena_blk {
    struct rmt_arena_blk *next;
    int used;
    trie_node_t nodes[];               /* flexible array member       */
} rmt_arena_blk_t;

static trie_node_t *node_alloc(rmt_table_t *t)
{
    rmt_arena_blk_t *b = (rmt_arena_blk_t *)t->arena;
    if (RMT_UNLIKELY(!b || b->used >= RMT_ARENA_NODES)) {
        b = (rmt_arena_blk_t *)calloc(1, sizeof(*b) +
                (size_t)RMT_ARENA_NODES * sizeof(trie_node_t));
        if (!b)
            return NULL;
        b->next = (rmt_arena_blk_t *)t->arena;
        t->arena = b;
    }
    return &b->nodes[b->used++];
}

/* ------------------------------------------------------------------ */
/* Bit helpers                                                         */
/* ------------------------------------------------------------------ */

/* load a 16-byte right-aligned big-endian value into two host u64s   */
static inline void kv_load(const uint8_t v[16], uint64_t *hi, uint64_t *lo)
{
    memcpy(hi, v, 8);      *hi = __builtin_bswap64(*hi);
    memcpy(lo, v + 8, 8);  *lo = __builtin_bswap64(*lo);
}

/* bit at absolute position p (0 = MSB) of the 128-bit (hi,lo) value  */
static inline int kv_bit(uint64_t hi, uint64_t lo, int p)
{
    return (p < 64) ? (int)((hi >> (63 - p)) & 1)
                    : (int)((lo >> (63 - (p - 64))) & 1);
}

/* bit i (0 = first bit) of a node's left-aligned label              */
static inline int label_bit(const trie_node_t *n, int i)
{
    return (n->label[i >> 3] >> (7 - (i & 7))) & 1;
}

static inline void label_set_bit(trie_node_t *n, int i, int b)
{
    uint8_t m = (uint8_t)(1u << (7 - (i & 7)));
    if (b) n->label[i >> 3] |= m;
    else   n->label[i >> 3] &= (uint8_t)~m;
}

/* Number of leading bits shared between n's label and the key starting
 * at absolute bit position abspos, capped at maxbits. Fast path: load
 * the label as a big-endian u64 pair, shift the key window to the MSB,
 * XOR and count leading zeros — no per-bit loop on the hot path.      */
static int common_prefix(const trie_node_t *n, uint64_t khi, uint64_t klo,
                         int abspos, int maxbits)
{
    int limit = n->label_len < maxbits ? n->label_len : maxbits;
    if (limit <= 0)
        return 0;

    uint64_t lhi, llo;
    memcpy(&lhi, n->label, 8);     lhi = __builtin_bswap64(lhi);
    memcpy(&llo, n->label + 8, 8); llo = __builtin_bswap64(llo);

    /* shift the 128-bit key left by abspos so key[abspos] is the MSB */
    uint64_t shi, slo;
    if (abspos == 0) {
        shi = khi; slo = klo;
    } else if (abspos < 64) {
        shi = (khi << abspos) | (klo >> (64 - abspos));
        slo = klo << abspos;
    } else if (abspos == 64) {
        shi = klo; slo = 0;
    } else {
        shi = klo << (abspos - 64); slo = 0;
    }

    uint64_t xhi = shi ^ lhi, xlo = slo ^ llo;
    int lz;
    if (xhi)               lz = __builtin_clzll(xhi);
    else if (xlo)          lz = 64 + __builtin_clzll(xlo);
    else                   lz = 128;
    return lz > limit ? limit : lz;
}

/* control-plane label builders (insert only; bit-at-a-time is fine)  */
static void copy_label_prefix(trie_node_t *dst, const trie_node_t *src, int c)
{
    dst->label_len = (uint16_t)c;
    for (int i = 0; i < c; i++)
        label_set_bit(dst, i, label_bit(src, i));
}

/* drop the first c bits of n's label (n keeps label[c..])            */
static void shift_label_left(trie_node_t *n, int c)
{
    int newlen = n->label_len - c;
    for (int i = 0; i < newlen; i++)
        label_set_bit(n, i, label_bit(n, i + c));
    n->label_len = (uint16_t)newlen;
}

/* child->label = key bits [abspos .. abspos+len)                     */
static void set_label_from_key(trie_node_t *child, uint64_t hi, uint64_t lo,
                               int abspos, int len)
{
    child->label_len = (uint16_t)len;
    for (int i = 0; i < len; i++)
        label_set_bit(child, i, kv_bit(hi, lo, abspos + i));
}

/* ------------------------------------------------------------------ */
/* Insert: radix walk with node splitting                              */
/* ------------------------------------------------------------------ */

/* Insert the prefix key[0..plen) (a width-wide field value) into the
 * trie rooted at root; returns the node at depth plen (creating or
 * splitting nodes as needed), where the caller attaches next_root or
 * the entry.                                                          */
static trie_node_t *radix_insert_field(rmt_table_t *t, trie_node_t *root,
                                       const uint8_t key[16],
                                       int width, int plen)
{
    uint64_t hi, lo;
    kv_load(key, &hi, &lo);
    int lead = 128 - width;           /* absolute MSB of field value  */

    trie_node_t *parent = NULL;
    int slot = 0;
    trie_node_t *n = root;
    int depth = 0;                    /* field bits consumed so far   */

    for (;;) {
        int rem = plen - depth;
        int c = common_prefix(n, hi, lo, lead + depth, rem);

        if (c < n->label_len) {
            /* n's label diverges from the key, or the key's prefix ends
             * inside n's label: split n at bit c.                     */
            trie_node_t *m = node_alloc(t);
            if (!m) return NULL;
            copy_label_prefix(m, n, c);            /* m = common part  */
            if (parent) parent->b[slot] = m;       /* m replaces n     */
            int firstbit = label_bit(n, c);        /* n's diverge bit  */
            shift_label_left(n, c);                /* n keeps label[c..] */
            m->b[firstbit] = n;
            depth += c;
            if (depth == plen)
                return m;                          /* prefix ends at m */
            int bit = kv_bit(hi, lo, lead + depth);
            trie_node_t *child = node_alloc(t);
            if (!child) return NULL;
            set_label_from_key(child, hi, lo, lead + depth, plen - depth);
            m->b[bit] = child;
            return child;
        }

        /* n's whole label matched */
        depth += n->label_len;
        if (depth == plen)
            return n;                              /* prefix ends at n */
        int bit = kv_bit(hi, lo, lead + depth);
        if (!n->b[bit]) {
            trie_node_t *child = node_alloc(t);
            if (!child) return NULL;
            set_label_from_key(child, hi, lo, lead + depth, plen - depth);
            n->b[bit] = child;
            return child;
        }
        parent = n; slot = bit; n = n->b[bit];
    }
}

rmt_entry_t *rmt_table_insert(rmt_pipeline_t *p, rmt_table_t *t,
                              const rmt_entry_t *tpl)
{
    if (!t->root) {
        t->root = node_alloc(t);
        if (!t->root)
            return NULL;
    }

    trie_node_t *n = t->root;
    for (int k = 0; k < t->nkeys; k++) {
        int width = p->fdefs[t->keys[k].field_id].bitlen;
        int plen  = tpl->plen[k];
        if (plen > width)
            return NULL;
        if (t->keys[k].mtype == MATCH_EXACT && plen != width) {
            fprintf(stderr, "table %s: key %d requires exact match\n",
                    t->name, k);
            return NULL;
        }
        n = radix_insert_field(t, n, tpl->key[k], width, plen);
        if (!n)
            return NULL;
        if (k < t->nkeys - 1) {
            if (!n->next_root) {
                n->next_root = node_alloc(t);
                if (!n->next_root)
                    return NULL;
            }
            n = n->next_root;
        }
    }

    rmt_entry_t *e = (rmt_entry_t *)calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    *e = *tpl;
    e->id = t->nentries++;
    /* first-inserted wins among identical keys */
    e->next = NULL;
    if (!n->entry) {
        n->entry = e;
    } else {
        rmt_entry_t *q = n->entry;
        while (q->next) q = q->next;
        q->next = e;
    }
    return e;
}

/* ------------------------------------------------------------------ */
/* Lookup: compressed walk + iterative cross-field backtracking        */
/* ------------------------------------------------------------------ */

/* Walk one key field's radix trie along the packet's key bits and
 * collect every "feasible point": a node carrying next_root (non-last
 * field) or an entry (last field). Candidates come out in walk order
 * (shallow -> deep), so popping from the end tries the longest prefix
 * first.                                                              */
static int field_candidates(trie_node_t *root, const fv_t *f, bool last,
                            trie_node_t **cand)
{
    int ncand = 0;
    int L = f->bitlen;
    uint64_t hi, lo;
    kv_load(f->v, &hi, &lo);
    int lead = 128 - L;

    trie_node_t *n = root;
    int depth = 0;
    while (n) {
        int rem = L - depth;
        int c = common_prefix(n, hi, lo, lead + depth, rem);
        if (c < n->label_len)
            break;                          /* label mismatch: stop    */
        depth += n->label_len;
        if (last ? (n->entry != NULL) : (n->next_root != NULL))
            cand[ncand++] = n;
        if (depth == L)
            break;
        int bit = kv_bit(hi, lo, lead + depth);
        trie_node_t *next = n->b[bit];
        if (RMT_LIKELY(next != NULL))
            RMT_PREFETCH(next);
        n = next;
    }
    return ncand;
}

/* iterative depth-first, longest-prefix-first lookup across all key
 * fields (README §3.4 semantics: lexicographic longest-prefix-first,
 * backtracking to shorter prefixes of an earlier field when a deeper
 * field has no completion).
 *
 * One pending-candidate list per key field sits on an explicit stack;
 * candidates are tried deepest first, and a dead end at field k pops
 * back to field k-1's next shallower candidate.                       */
rmt_entry_t *rmt_table_lookup(const rmt_pipeline_t *p __attribute__((unused)),
                              const rmt_table_t *t,
                              const phv_t *phv)
{
    if (!t->root || t->nkeys == 0)
        return NULL;

    struct {
        trie_node_t *cand[132];   /* path length <= bitlen+1 <= 129  */
        int ncand;
    } stk[RMT_MAX_KEYS];

    int kidx = 0;
    stk[0].ncand = field_candidates(t->root,
                                    &phv->fields[t->keys[0].field_id],
                                    t->nkeys == 1, stk[0].cand);
    for (;;) {
        if (stk[kidx].ncand == 0) {           /* dead end: backtrack  */
            if (kidx == 0)
                return NULL;
            kidx--;
            continue;
        }
        trie_node_t *c = stk[kidx].cand[--stk[kidx].ncand];
        if (kidx == t->nkeys - 1)             /* deepest entry = most */
            return c->entry;                  /* specific combined    */
        kidx++;
        stk[kidx].ncand = field_candidates(c->next_root,
                                           &phv->fields[t->keys[kidx].field_id],
                                           kidx == t->nkeys - 1,
                                           stk[kidx].cand);
    }
}

/* free the entry chains only; the nodes themselves live in the arena
 * and are released in bulk below.                                     */
static void node_free_entries(trie_node_t *n)
{
    if (!n)
        return;
    node_free_entries(n->b[0]);
    node_free_entries(n->b[1]);
    node_free_entries(n->next_root);
    rmt_entry_t *e = n->entry;
    while (e) {
        rmt_entry_t *nx = e->next;
        free(e);
        e = nx;
    }
}

void rmt_table_free(rmt_table_t *t)
{
    node_free_entries(t->root);
    rmt_arena_blk_t *b = (rmt_arena_blk_t *)t->arena;
    while (b) {
        rmt_arena_blk_t *nx = b->next;
        free(b);
        b = nx;
    }
    t->arena = NULL;
    t->root = NULL;
    t->nentries = 0;
}
