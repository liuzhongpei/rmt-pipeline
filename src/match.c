/*
 * match.c - tree-structured lookup engine
 *
 * Each match table's key is a list of fields (e.g. the 5-tuple:
 * src addr, dst addr, protocol, sport, dport). The lookup structure is
 * a chain of binary bit-tries, one trie per key field:
 *
 *   field0 trie --(terminal node)--> field1 trie --> ... --> entries
 *
 * A key field supports EXACT (prefix == full width) and LPM matching
 * (prefix length 0 acts as wildcard). Lookup walks each trie along the
 * packet's key bits, remembering every node that carries an attachment,
 * and backtracks longest-prefix-first across fields, so the most
 * specific combined rule wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rmt.h"

static trie_node_t *node_new(void)
{
    return (trie_node_t *)calloc(1, sizeof(trie_node_t));
}

/* bit i (0 = MSB) of a right-aligned value of width bitlen */
static int key_bit(const uint8_t v[16], int bitlen, int i)
{
    int p = 128 - bitlen + i;
    return (v[p >> 3] >> (7 - (p & 7))) & 1;
}

rmt_entry_t *rmt_table_insert(rmt_pipeline_t *p, rmt_table_t *t,
                              const rmt_entry_t *tpl)
{
    if (!t->root)
        t->root = node_new();

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
        /* walk/create plen bits of this field's trie */
        for (int i = 0; i < plen; i++) {
            int b = key_bit(tpl->key[k], width, i);
            if (!n->b[b])
                n->b[b] = node_new();
            n = n->b[b];
        }
        if (k < t->nkeys - 1) {
            if (!n->next_root)
                n->next_root = node_new();
            n = n->next_root;
        }
    }

    rmt_entry_t *e = (rmt_entry_t *)calloc(1, sizeof(*e));
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

/* iterative longest-prefix-first lookup across all key fields.
 * Uses an explicit stack to avoid function call overhead from
 * recursion (3-5 key fields × backtracking = many call frames). */
rmt_entry_t *rmt_table_lookup(const rmt_pipeline_t *p __attribute__((unused)),
                              const rmt_table_t *t,
                              const phv_t *phv)
{
    if (!t->root || t->nkeys == 0)
        return NULL;

    /* stack frame for iterative backtracking across key fields */
    struct {
        trie_node_t *cand[132];
        int ncand;
    } stk[RMT_MAX_KEYS];

    /* phase 1: walk each key field's trie, collect candidates */
    trie_node_t *root = t->root;
    for (int kidx = 0; kidx < t->nkeys; kidx++) {
        if (RMT_UNLIKELY(!root)) return NULL;

        const fv_t *f = &phv->fields[t->keys[kidx].field_id];
        int L = f->bitlen;
        bool last = (kidx == t->nkeys - 1);

        stk[kidx].ncand = 0;
        trie_node_t *n = root;
        int depth = 0;
        while (n) {
            if (last ? (n->entry != NULL) : (n->next_root != NULL))
                stk[kidx].cand[stk[kidx].ncand++] = n;
            if (depth == L)
                break;
            int bit = fv_get_bit(f, depth);
            trie_node_t *next = n->b[bit];
            if (RMT_LIKELY(next != NULL))
                RMT_PREFETCH(next);
            n = next;
            depth++;
        }

        /* for non-last fields, use the deepest match to go to next */
        if (!last) {
            if (stk[kidx].ncand == 0)
                return NULL;
            root = stk[kidx].cand[stk[kidx].ncand - 1]->next_root;
        }
    }

    /* phase 2: backtrack from last key field, longest-prefix-first */
    int lastk = t->nkeys - 1;
    for (int i = stk[lastk].ncand - 1; i >= 0; i--) {
        rmt_entry_t *e = stk[lastk].cand[i]->entry;
        if (e) return e;
    }
    return NULL;
}

static void node_free(trie_node_t *n)
{
    if (!n)
        return;
    node_free(n->b[0]);
    node_free(n->b[1]);
    node_free(n->next_root);
    rmt_entry_t *e = n->entry;
    while (e) {
        rmt_entry_t *nx = e->next;
        free(e);
        e = nx;
    }
    free(n);
}

void rmt_table_free(rmt_table_t *t)
{
    node_free(t->root);
    t->root = NULL;
    t->nentries = 0;
}
