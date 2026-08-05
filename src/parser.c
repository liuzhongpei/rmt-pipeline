/*
 * parser.c - programmable packet parser & deparser
 *
 * The parser is a table-driven finite state machine (parse graph).
 * Each state extracts one or more headers (bit-accurate) into the PHV
 * and selects the next state based on an extracted field value, exactly
 * like a P4 parser. Header stacks (e.g. the SRv6 segment list) are
 * extracted with a run-time count taken from a previously parsed field.
 *
 * The deparser re-serializes all valid (non-metadata) headers in
 * declaration order and appends the unparsed payload.
 */
#include <stdio.h>
#include <string.h>
#include "rmt.h"

/* read nbits from pkt starting at bit offset into fv (right aligned) */
static void pkt_get_bits(const uint8_t *pkt, int bitoff, int nbits, fv_t *out)
{
    out->bitlen = (uint16_t)nbits;

    /* fast path: byte-aligned field (the common case for real
     * protocol fields: 8/16/32/48/128 bit at byte offsets) */
    if (((bitoff | nbits) & 7) == 0) {
        int nb = nbits >> 3;
        memset(out->v, 0, (size_t)(16 - nb));
        memcpy(out->v + 16 - nb, pkt + (bitoff >> 3), (size_t)nb);
        return;
    }

    /* bit-exact slow path (e.g. mpls.label:20, ipv4.version:4) */
    fv_clear(out);
    for (int i = 0; i < nbits; i++) {
        int p = bitoff + i;
        int b = (pkt[p >> 3] >> (7 - (p & 7))) & 1;
        fv_set_bit(out, i, b);
    }
}

/* write nbits of fv into pkt at bit offset */
static void pkt_put_bits(uint8_t *pkt, int bitoff, const fv_t *in, int nbits)
{
    /* fast path: byte-aligned field, whole bytes copied out */
    if (((bitoff | nbits) & 7) == 0) {
        memcpy(pkt + (bitoff >> 3), in->v + 16 - (nbits >> 3),
               (size_t)(nbits >> 3));
        return;
    }

    for (int i = 0; i < nbits; i++) {
        int p = bitoff + i;
        uint8_t m = (uint8_t)(1u << (7 - (p & 7)));
        /* fv bit i relative to its own bitlen */
        int src = 128 - nbits + i;
        int b = (in->v[src >> 3] >> (7 - (src & 7))) & 1;
        if (b) pkt[p >> 3] |= m;
        else   pkt[p >> 3] &= (uint8_t)~m;
    }
}

static int extract_hdr(const rmt_pipeline_t *p, int hdr, const uint8_t *pkt,
                       int len, int *bitoff, phv_t *phv)
{
    const hdr_def_t *h = &p->hdefs[hdr];
    if (*bitoff + h->bitlen > len * 8) {
        fprintf(stderr, "parser: packet too short for header %s\n", h->name);
        return -1;
    }

    /* fast path: entire header is byte-aligned (most real headers).
     * Extract all fields with pointer arithmetic, one memcpy each,
     * no per-bit function call overhead.                              */
    if (((*bitoff | h->bitlen) & 7) == 0) {
        const uint8_t *base = pkt + (*bitoff >> 3);
        int boff = 0;
        for (int i = 0; i < h->nfields; i++) {
            int fid = h->first_field + i;
            int blen = p->fdefs[fid].bitlen;
            /* if the individual field is also byte-aligned (99% case) */
            if (RMT_LIKELY((blen & 7) == 0)) {
                int nb = blen >> 3;
                fv_t *fv = &phv->fields[fid];
                fv->bitlen = (uint16_t)blen;
                memset(fv->v, 0, (size_t)(16 - nb));
                memcpy(fv->v + 16 - nb, base + (boff >> 3), (size_t)nb);
            } else {
                pkt_get_bits(pkt, *bitoff + boff, blen,
                             &phv->fields[fid]);
            }
            boff += blen;
        }
        *bitoff += h->bitlen;
    } else {
        /* slow path: non-byte-aligned header (rare) */
        for (int i = 0; i < h->nfields; i++) {
            int fid = h->first_field + i;
            pkt_get_bits(pkt, *bitoff, p->fdefs[fid].bitlen,
                         &phv->fields[fid]);
            *bitoff += p->fdefs[fid].bitlen;
        }
    }

    phv_hdr_set(phv, hdr);
    if (RMT_UNLIKELY(p->verbose))
        printf("    [parser] extract %-10s (%d bits)\n", h->name, h->bitlen);
    return 0;
}

int rmt_parse(const rmt_pipeline_t *p, const uint8_t *pkt, int len,
              phv_t *phv, int *hdr_bytes)
{
    int st = p->start_state;
    int bitoff = 0;
    int guard = 0;

    while (st != PS_ACCEPT) {
        if (st < 0 || st >= p->nstates || ++guard > 64) {
            fprintf(stderr, "parser: bad state / loop\n");
            return -1;
        }
        const parse_state_t *s = &p->states[st];
        if (RMT_UNLIKELY(p->verbose))
            printf("  [parser] state %s\n", s->name);

        for (int i = 0; i < s->nops; i++) {
            const parse_op_t *op = &s->ops[i];
            if (op->kind == POP_EXTRACT) {
                if (extract_hdr(p, op->hdr, pkt, len, &bitoff, phv) < 0)
                    return -1;
            } else { /* POP_EXTRACT_ARRAY */
                uint64_t cnt = fv_get_u64(&phv->fields[op->count_field])
                               + (uint64_t)op->count_add;
                /* find the array this header belongs to, clamp count */
                for (int a = 0; a < p->narrs; a++)
                    if (p->adefs[a].first_hdr == op->hdr &&
                        cnt > (uint64_t)p->adefs[a].count)
                        cnt = (uint64_t)p->adefs[a].count;
                /* hard cap: never exceed header table bounds */
                int max_cnt = (op->hdr < p->nhdrs)
                            ? (p->nhdrs - op->hdr) : 0;
                if (cnt > (uint64_t)max_cnt)
                    cnt = (uint64_t)max_cnt;
                for (uint64_t k = 0; k < cnt; k++)
                    if (extract_hdr(p, op->hdr + (int)k, pkt, len,
                                    &bitoff, phv) < 0)
                        return -1;
            }
        }

        if (s->sel_field < 0) {
            st = s->def_next;
        } else {
            uint64_t v = fv_get_u64(&phv->fields[s->sel_field]);
            int nxt = s->def_next;
            for (int c = 0; c < s->ncases; c++)
                if (s->cases[c].val == v) { nxt = s->cases[c].next; break; }
            st = nxt;
        }
    }

    *hdr_bytes = (bitoff + 7) / 8;
    return 0;
}

int rmt_deparse(const rmt_pipeline_t *p, const phv_t *phv,
                uint8_t *out, int cap)
{
    int bitoff = 0;
    for (int h = 0; h < p->nhdrs; h++) {
        const hdr_def_t *hd = &p->hdefs[h];
        if (hd->is_meta || !phv_hdr_valid(phv, h))
            continue;
        if ((bitoff + hd->bitlen + 7) / 8 > cap)
            return -1;
        for (int i = 0; i < hd->nfields; i++) {
            int fid = hd->first_field + i;
            pkt_put_bits(out, bitoff, &phv->fields[fid],
                         p->fdefs[fid].bitlen);
            bitoff += p->fdefs[fid].bitlen;
        }
    }
    /* the output buffer is not pre-zeroed: clear any trailing bits
     * of the last partial byte (headers ending off a byte boundary) */
    if (bitoff & 7)
        out[bitoff >> 3] &= (uint8_t)(0xff00u >> (bitoff & 7));
    return (bitoff + 7) / 8;
}
