/*
 * alu.c - RISC action engine: 16 parallel ALUs per stage (VLIW model)
 *
 * An action is compiled by p4c into a micro-program of VLIW words.
 * Each word carries up to RMT_NUM_ALUS (16) independent RISC
 * instructions, one per ALU. All ALUs of a word execute in parallel:
 * every operand is read from the PHV *before* any result is committed,
 * which gives true parallel (read-before-write) semantics, exactly as
 * hardware ALUs latching their operands on the same clock edge.
 *
 * The PHV is the shared register file; per-entry action data (params)
 * feed the immediate port of an ALU when insn.use_param is set.
 */
#include <stdio.h>
#include <string.h>
#include "rmt.h"

/* safe accessors: defence-in-depth against corrupt imm indices */
static inline rmt_cache_t *safe_cache(const rmt_pipeline_t *p, int64_t i)
{
    return (i >= 0 && i < p->ncaches) ? p->caches[i] : NULL;
}
static inline rmt_dnn_t *safe_dnn(const rmt_pipeline_t *p, int64_t i)
{
    return (i >= 0 && i < p->ndnns) ? p->dnns[i] : NULL;
}

static const char *op_name[] = {
    "nop", "movi", "mov", "add", "sub", "and", "or", "xor", "shl", "shr",
    "addi", "subi", "andi", "ori", "xori", "shli", "shri",
    "movx", "validate", "invalidate",
    "cs_lookup", "cs_insert", "pit_insert", "pit_consume",
    "util_mark", "util_probe",
    "sketch_inc", "sketch_get",
    "dnn_infer"
};

typedef struct {
    int  kind;      /* 0: none, 1: field write, 2: validity write */
    int  fd;        /* field id (kind 1) or header id (kind 2)    */
    fv_t val;
    bool valid;
} wb_t;

RMT_HOT
void rmt_action_exec(const rmt_pipeline_t *p, const rmt_action_t *a,
                     const uint64_t *params, phv_t *phv)
{
#ifdef __GNUC__
    /* computed goto dispatch table: eliminates branch prediction
     * misses from the large switch; each opcode jumps directly to
     * its handler via an indirect branch from a static table.     */
    static const void *dispatch[] = {
        [OP_NOP] = &&L_NOP, [OP_MOVI] = &&L_MOVI, [OP_MOV] = &&L_MOV,
        [OP_ADD] = &&L_ADD, [OP_SUB] = &&L_SUB, [OP_AND] = &&L_AND,
        [OP_OR] = &&L_OR, [OP_XOR] = &&L_XOR, [OP_SHL] = &&L_SHL,
        [OP_SHR] = &&L_SHR, [OP_ADDI] = &&L_ADDI, [OP_SUBI] = &&L_SUBI,
        [OP_ANDI] = &&L_ANDI, [OP_ORI] = &&L_ORI, [OP_XORI] = &&L_XORI,
        [OP_SHLI] = &&L_SHLI, [OP_SHRI] = &&L_SHRI, [OP_MOVX] = &&L_MOVX,
        [OP_VALID] = &&L_VALID, [OP_INVALID] = &&L_INVALID,
        [OP_CS_LOOKUP] = &&L_CS_LOOKUP, [OP_CS_INSERT] = &&L_CS_INSERT,
        [OP_PIT_INSERT] = &&L_PIT_INSERT, [OP_PIT_CONSUME] = &&L_PIT_CONSUME,
        [OP_UTIL_MARK] = &&L_UTIL_MARK, [OP_UTIL_PROBE] = &&L_UTIL_PROBE,
        [OP_SKETCH_INC] = &&L_SKETCH_INC, [OP_SKETCH_GET] = &&L_SKETCH_GET,
        [OP_DNN_INFER] = &&L_DNN_INFER
    };
#endif

    for (int w = 0; w < a->nwords; w++) {
        wb_t wb[RMT_NUM_ALUS];
        int nwb = a->nslots[w];
        if (nwb < 0 || nwb > RMT_NUM_ALUS)   /* bounds guard */
            nwb = RMT_NUM_ALUS;

        /* ---- execute all 16 ALUs of this word in parallel ---- */
        for (int s = 0; s < nwb; s++) {
            const alu_insn_t *in = &a->prog[w][s];
            wb_t *o = &wb[s];
            o->kind = 0;

            int64_t imm = in->use_param ? (int64_t)params[in->pidx]
                                        : in->imm;
            uint64_t s1 = (in->fs1 >= 0)
                          ? fv_get_u64(&phv->fields[in->fs1]) : 0;
            uint64_t s2 = (in->fs2 >= 0)
                          ? fv_get_u64(&phv->fields[in->fs2]) : 0;
            uint64_t r = 0;
            bool wide = false;

#ifdef __GNUC__
            goto *dispatch[in->op];
            L_NOP: continue;
            L_MOVI: r = (uint64_t)imm; goto L_COMMIT;
            L_MOV:
                o->kind = 1; o->fd = in->fd;
                o->val = phv->fields[in->fd];
                fv_copy(&o->val, &phv->fields[in->fs1]);
                wide = true; goto L_COMMIT;
            L_ADD:  r = s1 + s2;  goto L_COMMIT;
            L_SUB:  r = s1 - s2;  goto L_COMMIT;
            L_AND:  r = s1 & s2;  goto L_COMMIT;
            L_OR:   r = s1 | s2;  goto L_COMMIT;
            L_XOR:  r = s1 ^ s2;  goto L_COMMIT;
            L_SHL:  r = s1 << (s2 & 63); goto L_COMMIT;
            L_SHR:  r = s1 >> (s2 & 63); goto L_COMMIT;
            L_ADDI: r = s1 + (uint64_t)imm; goto L_COMMIT;
            L_SUBI: r = s1 - (uint64_t)imm; goto L_COMMIT;
            L_ANDI: r = s1 & (uint64_t)imm; goto L_COMMIT;
            L_ORI:  r = s1 | (uint64_t)imm; goto L_COMMIT;
            L_XORI: r = s1 ^ (uint64_t)imm; goto L_COMMIT;
            L_SHLI: r = s1 << (imm & 63); goto L_COMMIT;
            L_SHRI: r = s1 >> (imm & 63); goto L_COMMIT;
            L_MOVX: {
                uint64_t idx = s1;
                if (in->imm <= 0 || idx >= (uint64_t)in->imm)
                    continue;
                int src = in->fbase + (int)idx * in->stride;
                if (src < 0 || src >= RMT_MAX_FIELDS)
                    continue;
                o->kind = 1; o->fd = in->fd;
                o->val = phv->fields[in->fd];
                fv_copy(&o->val, &phv->fields[src]);
                wide = true; goto L_COMMIT;
            }
            L_VALID:
                o->kind = 2; o->fd = in->fd; o->valid = true;
                goto L_COMMIT;
            L_INVALID:
                o->kind = 2; o->fd = in->fd; o->valid = false;
                goto L_COMMIT;
            L_CS_LOOKUP: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                int len = 0;
                if (rmt_cache_get(cc, &phv->fields[in->fs1], phv->inject,
                                  RMT_PKT_MAX, &len, NULL)) {
                    phv->inject_len = len;
                    phv->has_inject = true;
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    goto L_COMMIT;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] cs_lookup MISS\n", s, w);
                continue;
            }
            L_CS_INSERT: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                rmt_cache_put(cc, &phv->fields[in->fs1],
                              phv->pl, phv->pl_len, 0, false);
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] cs_insert %dB content\n",
                           s, w, phv->pl_len);
                continue;
            }
            L_PIT_INSERT: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                if (!rmt_cache_put(cc, &phv->fields[in->fs1],
                                   NULL, 0, s2, true)) {
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    goto L_COMMIT;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] pit_insert port %llu\n",
                           s, w, (unsigned long long)s2);
                continue;
            }
            L_PIT_CONSUME: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                uint64_t port = 0;
                o->kind = 1;
                if (rmt_cache_take(cc,
                                   &phv->fields[in->fs1], &port)) {
                    o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, port);
                } else if (p->fid_drop >= 0) {
                    o->fd = (int16_t)p->fid_drop;
                    o->val = phv->fields[o->fd];
                    fv_set_u64(&o->val, 1);
                } else {
                    o->kind = 0;
                }
                goto L_COMMIT;
            }
            L_UTIL_MARK: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                rmt_cache_mark(cc, &phv->fields[in->fs1]);
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] util_mark addr used\n",
                           s, w);
                continue;
            }
            L_UTIL_PROBE: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                if (rmt_cache_probe(cc,
                                    &phv->fields[in->fs1])) {
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    goto L_COMMIT;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] util_probe UNUSED\n", s, w);
                continue;
            }
            L_DNN_INFER: {
                rmt_dnn_t *dn = safe_dnn(p, in->imm);
                if (!dn) continue;
                uint64_t x[RMT_DNN_MAX_IN];
                int nin = rmt_dnn_num_inputs(dn);
                for (int i = 0; i < nin; i++)
                    x[i] = fv_get_u64(
                        &phv->fields[rmt_dnn_input_fid(dn, i)]);
                r = (uint64_t)rmt_dnn_infer(dn, x);
                goto L_COMMIT;
            }
            L_SKETCH_INC: {
                /* atomic increment of d counters (fire-and-forget) */
                if (in->imm >= 0 && in->imm < p->nsketches)
                    rmt_sketch_inc(p->sketches[in->imm], phv);
                continue;
            }
            L_SKETCH_GET: {
                /* estimate flow frequency → write min to fd */
                if (in->imm >= 0 && in->imm < p->nsketches) {
                    r = (uint64_t)rmt_sketch_get(p->sketches[in->imm], phv);
                    goto L_COMMIT;
                }
                continue;
            }

            L_COMMIT:
#else
            /* fallback: standard switch for non-GCC compilers */
            switch (in->op) {
            case OP_NOP:
                continue;
            case OP_MOVI: r = (uint64_t)imm; break;
            case OP_MOV:
                o->kind = 1; o->fd = in->fd;
                o->val = phv->fields[in->fd];
                fv_copy(&o->val, &phv->fields[in->fs1]);
                wide = true;
                break;
            case OP_ADD:  r = s1 + s2;  break;
            case OP_SUB:  r = s1 - s2;  break;
            case OP_AND:  r = s1 & s2;  break;
            case OP_OR:   r = s1 | s2;  break;
            case OP_XOR:  r = s1 ^ s2;  break;
            case OP_SHL:  r = s1 << (s2 & 63); break;
            case OP_SHR:  r = s1 >> (s2 & 63); break;
            case OP_ADDI: r = s1 + (uint64_t)imm; break;
            case OP_SUBI: r = s1 - (uint64_t)imm; break;
            case OP_ANDI: r = s1 & (uint64_t)imm; break;
            case OP_ORI:  r = s1 | (uint64_t)imm; break;
            case OP_XORI: r = s1 ^ (uint64_t)imm; break;
            case OP_SHLI: r = s1 << (imm & 63); break;
            case OP_SHRI: r = s1 >> (imm & 63); break;
            case OP_MOVX: {
                uint64_t idx = s1;
                if (in->imm <= 0 || idx >= (uint64_t)in->imm)
                    continue;
                int src = in->fbase + (int)idx * in->stride;
                if (src < 0 || src >= RMT_MAX_FIELDS)
                    continue;
                o->kind = 1; o->fd = in->fd;
                o->val = phv->fields[in->fd];
                fv_copy(&o->val, &phv->fields[src]);
                wide = true;
                break;
            }
            case OP_VALID:
                o->kind = 2; o->fd = in->fd; o->valid = true;
                break;
            case OP_INVALID:
                o->kind = 2; o->fd = in->fd; o->valid = false;
                break;
            case OP_CS_LOOKUP: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                int len = 0;
                if (rmt_cache_get(cc, &phv->fields[in->fs1], phv->inject,
                                  RMT_PKT_MAX, &len, NULL)) {
                    phv->inject_len = len;
                    phv->has_inject = true;
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    break;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] cs_lookup MISS\n", s, w);
                continue;
            }
            case OP_CS_INSERT: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                rmt_cache_put(cc, &phv->fields[in->fs1],
                              phv->pl, phv->pl_len, 0, false);
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] cs_insert %dB content\n",
                           s, w, phv->pl_len);
                continue;
            }
            case OP_PIT_INSERT: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                if (!rmt_cache_put(cc, &phv->fields[in->fs1],
                                   NULL, 0, s2, true)) {
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    break;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] pit_insert port %llu\n",
                           s, w, (unsigned long long)s2);
                continue;
            }
            case OP_PIT_CONSUME: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                uint64_t port = 0;
                o->kind = 1;
                if (rmt_cache_take(cc,
                                   &phv->fields[in->fs1], &port)) {
                    o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, port);
                } else if (p->fid_drop >= 0) {
                    o->fd = (int16_t)p->fid_drop;
                    o->val = phv->fields[o->fd];
                    fv_set_u64(&o->val, 1);
                } else {
                    o->kind = 0;
                }
                break;
            }
            case OP_UTIL_MARK: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                rmt_cache_mark(cc, &phv->fields[in->fs1]);
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] util_mark addr used\n",
                           s, w);
                continue;
            }
            case OP_UTIL_PROBE: {
                rmt_cache_t *cc = safe_cache(p, in->imm);
                if (!cc) continue;
                if (rmt_cache_probe(cc,
                                    &phv->fields[in->fs1])) {
                    o->kind = 1; o->fd = in->fd;
                    o->val = phv->fields[in->fd];
                    fv_set_u64(&o->val, 1);
                    break;
                }
                if (RMT_UNLIKELY(p->verbose))
                    printf("      [alu%-2d w%d] util_probe UNUSED\n", s, w);
                continue;
            }
            case OP_DNN_INFER: {
                rmt_dnn_t *dn = safe_dnn(p, in->imm);
                if (!dn) continue;
                uint64_t x[RMT_DNN_MAX_IN];
                int nin = rmt_dnn_num_inputs(dn);
                for (int i = 0; i < nin; i++)
                    x[i] = fv_get_u64(
                        &phv->fields[rmt_dnn_input_fid(dn, i)]);
                r = (uint64_t)rmt_dnn_infer(dn, x);
                break;
            }
            case OP_SKETCH_INC: {
                if (in->imm >= 0 && in->imm < p->nsketches)
                    rmt_sketch_inc(p->sketches[in->imm], phv);
                continue;
            }
            case OP_SKETCH_GET: {
                if (in->imm >= 0 && in->imm < p->nsketches) {
                    r = (uint64_t)rmt_sketch_get(p->sketches[in->imm], phv);
                    break;
                }
                continue;
            }
            }
#endif

            if (o->kind == 0 && in->op != OP_VALID && in->op != OP_INVALID) {
                o->kind = 1;
                o->fd = in->fd;
                o->val = phv->fields[in->fd];
                if (!wide)
                    fv_set_u64(&o->val, r);
            }

            if (RMT_UNLIKELY(p->verbose)) {
                char b[40];
                if (o->kind == 1)
                    printf("      [alu%-2d w%d] %-5s %-24s = %s\n", s, w,
                           op_name[in->op], p->fdefs[o->fd].name,
                           fv_str(&o->val, b, sizeof(b)));
                else
                    printf("      [alu%-2d w%d] %-5s header %s\n", s, w,
                           op_name[in->op], p->hdefs[o->fd].name);
            }
        }

        /* ---- commit phase: all writes land simultaneously ---- */
        for (int s = 0; s < nwb; s++) {
            if (wb[s].kind == 1) {
                phv->fields[wb[s].fd] = wb[s].val;
                phv_field_dirty(phv, wb[s].fd);      /* in-place track */
            } else if (wb[s].kind == 2) {
                phv->structural = true;              /* hdr add/remove */
                if (wb[s].valid)
                    phv_hdr_set(phv, wb[s].fd);
                else
                    phv_hdr_clr(phv, wb[s].fd);
            }
        }
    }
}
