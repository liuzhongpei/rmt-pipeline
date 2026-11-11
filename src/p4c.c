/*
 * p4c.c - P4 (subset) compiler targeting the RMT software pipeline
 *
 * Supported program structure (P4-14 flavored subset, see p4/srv6.p4):
 *
 *   header   NAME [ "[" N "]" ] { field : width; ... }
 *   metadata NAME { field : width; ... }
 *   parser   NAME { extract(hdr); extract_array(stk, fref [+ K]);
 *                   select(fref) { VAL: state; default: state; } | goto state; }
 *   action   NAME(p1, ...) { set(f, x); add(f, x); sub(f, x);
 *                            and/or/xor/shl/shr(f, x);
 *                            copy_indexed(f, stk.fld, idxf);
 *                            add_header(h); remove_header(h); }
 *   table    NAME { key { fref: exact|lpm; ... }
 *                   actions { a1; a2; ... }
 *                   default_action aN(v...); }
 *   control  ingress { apply(t); if (valid(h)) { apply(t); } ... }
 *
 * Compilation results:
 *   - headers/metadata  -> PHV field allocation (symbol tables)
 *   - parser states     -> programmable parse graph configuration
 *   - actions           -> RISC ALU micro-programs; primitives are
 *                          list-scheduled into VLIW words of up to 16
 *                          slots (data-independent primitives share a
 *                          word and run on the 16 ALUs in parallel)
 *   - tables            -> tree lookup engine key/action configuration
 *   - control ingress   -> physical stage assignment (one stage per
 *                          apply, in program order) + validity gates
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include "rmt.h"

enum { TK_EOF, TK_ID, TK_NUM, TK_PUNCT };

typedef struct {
    char *src;
    size_t len, pos;
    int line;
    int tt;
    char text[128];
    uint64_t num;
    rmt_pipeline_t *pipe;
    int next_stage;
    jmp_buf err;
} p4c_t;

/* ------------------------------------------------------------------ */
/* lexer                                                              */
/* ------------------------------------------------------------------ */
static void perr(p4c_t *c, const char *msg)
{
    fprintf(stderr, "p4c: line %d: error: %s (near '%s')\n",
            c->line, msg, c->text);
    longjmp(c->err, 1);
}

static void next(p4c_t *c)
{
    /* skip whitespace and comments */
    for (;;) {
        while (c->pos < c->len && isspace((unsigned char)c->src[c->pos])) {
            if (c->src[c->pos] == '\n')
                c->line++;
            c->pos++;
        }
        if (c->pos + 1 < c->len && c->src[c->pos] == '/' &&
            c->src[c->pos + 1] == '/') {
            while (c->pos < c->len && c->src[c->pos] != '\n')
                c->pos++;
            continue;
        }
        if (c->pos + 1 < c->len && c->src[c->pos] == '/' &&
            c->src[c->pos + 1] == '*') {
            c->pos += 2;
            while (c->pos + 1 < c->len &&
                   !(c->src[c->pos] == '*' && c->src[c->pos + 1] == '/')) {
                if (c->src[c->pos] == '\n')
                    c->line++;
                c->pos++;
            }
            if (c->pos + 1 >= c->len)
                perr(c, "unterminated /* */ comment");
            c->pos += 2;
            continue;
        }
        break;
    }

    if (c->pos >= c->len) {
        c->tt = TK_EOF;
        strcpy(c->text, "<eof>");
        return;
    }

    char ch = c->src[c->pos];
    if (isalpha((unsigned char)ch) || ch == '_') {
        size_t n = 0;
        while (c->pos < c->len &&
               (isalnum((unsigned char)c->src[c->pos]) ||
                c->src[c->pos] == '_')) {
            if (n < sizeof(c->text) - 1)
                c->text[n++] = c->src[c->pos];
            c->pos++;
        }
        c->text[n] = 0;
        c->tt = TK_ID;
        return;
    }
    if (isdigit((unsigned char)ch)) {
        char *end;
        c->num = strtoull(c->src + c->pos, &end, 0);
        size_t n = (size_t)(end - (c->src + c->pos));
        snprintf(c->text, sizeof(c->text), "%.*s", (int)n, c->src + c->pos);
        c->pos += n;
        c->tt = TK_NUM;
        return;
    }
    c->text[0] = ch;
    c->text[1] = 0;
    c->pos++;
    c->tt = TK_PUNCT;
}

static bool is_punct(p4c_t *c, char ch)
{
    return c->tt == TK_PUNCT && c->text[0] == ch;
}

static void expect_punct(p4c_t *c, char ch)
{
    if (!is_punct(c, ch)) {
        char m[64];
        snprintf(m, sizeof(m), "expected '%c'", ch);
        perr(c, m);
    }
    next(c);
}

static void expect_id(p4c_t *c, char *out, size_t n)
{
    if (c->tt != TK_ID)
        perr(c, "expected identifier");
    size_t l = strlen(c->text);
    if (l >= n)
        l = n - 1;
    memcpy(out, c->text, l);
    out[l] = 0;
    next(c);
}

static uint64_t expect_num(p4c_t *c)
{
    if (c->tt != TK_NUM)
        perr(c, "expected number");
    uint64_t v = c->num;
    next(c);
    return v;
}

static bool accept_kw(p4c_t *c, const char *kw)
{
    if (c->tt == TK_ID && !strcmp(c->text, kw)) {
        next(c);
        return true;
    }
    return false;
}

/* header reference: NAME or NAME[IDX] (header stack instance) */
static void expect_hdrref(p4c_t *c, char *out, size_t n)
{
    char base[RMT_NAME_LEN];
    expect_id(c, base, sizeof(base));
    if (is_punct(c, '[')) {
        next(c);
        int k = (int)expect_num(c);
        expect_punct(c, ']');
        snprintf(out, n, "%.47s[%d]", base, k);
    } else {
        snprintf(out, n, "%.47s", base);
    }
}

/* field reference: hdr.field / stack[i].field (bare stack name -> [0]) */
static int expect_fieldref(p4c_t *c)
{
    char h[RMT_NAME_LEN + 16], f[RMT_NAME_LEN], full[4 * RMT_NAME_LEN];
    expect_hdrref(c, h, sizeof(h));
    expect_punct(c, '.');
    expect_id(c, f, sizeof(f));

    snprintf(full, sizeof(full), "%s.%s", h, f);
    int fid = rmt_field_id(c->pipe, full);
    if (fid < 0 && rmt_arr_id(c->pipe, h) >= 0) {
        snprintf(full, sizeof(full), "%s[0].%s", h, f);
        fid = rmt_field_id(c->pipe, full);
    }
    if (fid < 0)
        perr(c, "unknown field reference");
    return fid;
}

/* ------------------------------------------------------------------ */
/* header / metadata declarations                                     */
/* ------------------------------------------------------------------ */
static void parse_header(p4c_t *c, bool is_meta)
{
    rmt_pipeline_t *p = c->pipe;
    char name[RMT_NAME_LEN];
    int arr = 0;

    expect_id(c, name, sizeof(name));
    if (is_punct(c, '[')) {
        next(c);
        arr = (int)expect_num(c);
        expect_punct(c, ']');
        if (arr < 1 || arr > 16)
            perr(c, "bad header stack size");
    }

    struct { char fname[RMT_NAME_LEN]; int bits; } flds[24];
    int nf = 0;

    expect_punct(c, '{');
    while (!is_punct(c, '}')) {
        if (nf >= 24)
            perr(c, "too many fields");
        expect_id(c, flds[nf].fname, sizeof(flds[nf].fname));
        expect_punct(c, ':');
        flds[nf].bits = (int)expect_num(c);
        expect_punct(c, ';');
        nf++;
    }
    next(c); /* '}' */

    if (arr == 0) {
        if (rmt_hdr_id(p, name) >= 0)
            perr(c, "duplicate header name");
        int h = rmt_hdr_add(p, name, is_meta);
        if (h < 0)
            perr(c, "too many headers");
        for (int i = 0; i < nf; i++) {
            char full[4 * RMT_NAME_LEN];
            snprintf(full, sizeof(full), "%s.%.47s", name, flds[i].fname);
            if (rmt_field_add(p, h, full, flds[i].bits) < 0)
                perr(c, "field allocation failed");
        }
    } else {
        if (p->narrs >= RMT_MAX_ARRAYS)
            perr(c, "too many header stacks");
        arr_def_t *a = &p->adefs[p->narrs++];
        snprintf(a->name, sizeof(a->name), "%s", name);
        a->count = arr;
        for (int k = 0; k < arr; k++) {
            char iname[RMT_NAME_LEN + 16];
            snprintf(iname, sizeof(iname), "%s[%d]", name, k);
            int h = rmt_hdr_add(p, iname, is_meta);
            if (h < 0)
                perr(c, "too many headers");
            if (k == 0)
                a->first_hdr = h;
            for (int i = 0; i < nf; i++) {
                char full[4 * RMT_NAME_LEN];
                snprintf(full, sizeof(full), "%s.%.47s", iname, flds[i].fname);
                if (rmt_field_add(p, h, full, flds[i].bits) < 0)
                    perr(c, "field allocation failed");
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* parser declarations                                                */
/* ------------------------------------------------------------------ */
static int state_get(p4c_t *c, const char *name)
{
    rmt_pipeline_t *p = c->pipe;
    if (!strcmp(name, "accept"))
        return PS_ACCEPT;
    for (int i = 0; i < p->nstates; i++)
        if (!strcmp(p->states[i].name, name))
            return i;
    if (p->nstates >= RMT_MAX_STATES)
        perr(c, "too many parser states");
    parse_state_t *s = &p->states[p->nstates];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->sel_field = -1;
    s->def_next = PS_ACCEPT;
    return p->nstates++;
}

static void parse_parser(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    char name[RMT_NAME_LEN];
    expect_id(c, name, sizeof(name));
    int si = state_get(c, name);
    if (!strcmp(name, "start"))
        p->start_state = si;

    expect_punct(c, '{');
    parse_state_t *s = &p->states[si];

    while (!is_punct(c, '}')) {
        if (accept_kw(c, "extract")) {
            expect_punct(c, '(');
            char h[RMT_NAME_LEN + 16];
            expect_hdrref(c, h, sizeof(h));
            expect_punct(c, ')');
            expect_punct(c, ';');
            int hid = rmt_hdr_id(p, h);
            if (hid < 0)
                perr(c, "unknown header in extract()");
            if (s->nops >= 4)
                perr(c, "too many parser ops in state");
            s->ops[s->nops++] = (parse_op_t){ POP_EXTRACT, hid, -1, 0 };
        } else if (accept_kw(c, "extract_array")) {
            expect_punct(c, '(');
            char an[RMT_NAME_LEN];
            expect_id(c, an, sizeof(an));
            expect_punct(c, ',');
            int cf = expect_fieldref(c);
            int add = 0;
            if (is_punct(c, '+')) {
                next(c);
                add = (int)expect_num(c);
            }
            expect_punct(c, ')');
            expect_punct(c, ';');
            int ai = rmt_arr_id(p, an);
            if (ai < 0)
                perr(c, "unknown header stack in extract_array()");
            if (s->nops >= 4)
                perr(c, "too many parser ops in state");
            s->ops[s->nops++] = (parse_op_t){
                POP_EXTRACT_ARRAY, p->adefs[ai].first_hdr, cf, add
            };
        } else if (accept_kw(c, "select")) {
            expect_punct(c, '(');
            s->sel_field = expect_fieldref(c);
            expect_punct(c, ')');
            expect_punct(c, '{');
            while (!is_punct(c, '}')) {
                if (accept_kw(c, "default")) {
                    expect_punct(c, ':');
                    char t[RMT_NAME_LEN];
                    expect_id(c, t, sizeof(t));
                    expect_punct(c, ';');
                    s->def_next = state_get(c, t);
                    s = &p->states[si]; /* state_get may realloc slots */
                } else {
                    uint64_t v = expect_num(c);
                    expect_punct(c, ':');
                    char t[RMT_NAME_LEN];
                    expect_id(c, t, sizeof(t));
                    expect_punct(c, ';');
                    int nx = state_get(c, t);
                    s = &p->states[si];
                    if (s->ncases >= 16)
                        perr(c, "too many select cases");
                    s->cases[s->ncases++] = (sel_case_t){ v, nx };
                }
            }
            next(c); /* '}' of select */
        } else if (accept_kw(c, "goto")) {
            char t[RMT_NAME_LEN];
            expect_id(c, t, sizeof(t));
            expect_punct(c, ';');
            s->def_next = state_get(c, t);
            s = &p->states[si];
        } else {
            perr(c, "unexpected token in parser state");
        }
    }
    next(c); /* '}' */
}

/* ------------------------------------------------------------------ */
/* actions: primitives -> RISC ALU instructions, VLIW list scheduling */
/* ------------------------------------------------------------------ */
static void sched(p4c_t *c, rmt_action_t *a, alu_insn_t in)
{
    int dep = 0;

    /* RAW / WAW analysis against already-scheduled instructions */
    for (int w = 0; w < a->nwords; w++) {
        for (int s = 0; s < a->nslots[w]; s++) {
            const alu_insn_t *e = &a->prog[w][s];
            if (e->op == OP_VALID || e->op == OP_INVALID)
                continue;
            int wfd = e->fd;
            if (wfd < 0) {
                /* fd=-1: fire-and-forget extern ops (cs_insert, util_mark,
                 * util_probe) that write no PHV field. Safe to skip
                 * dependency tracking: they have no observable effect on
                 * subsequent instructions in the same action. */
                continue;
            }
            bool conflict = false;
            if (in.op != OP_VALID && in.op != OP_INVALID) {
                if (wfd == in.fs1 || wfd == in.fs2 || wfd == in.fd)
                    conflict = true;
                if (in.op == OP_MOVX && wfd >= in.fbase &&
                    wfd < in.fbase + (int)in.imm * in.stride)
                    conflict = true;
            }
            if (conflict && w + 1 > dep)
                dep = w + 1;
        }
    }

    int w = dep;
    while (w < RMT_MAX_WORDS && a->nslots[w] >= RMT_NUM_ALUS)
        w++;
    if (w >= RMT_MAX_WORDS)
        perr(c, "action too large for ALU program memory");
    a->prog[w][a->nslots[w]++] = in;
    if (w + 1 > a->nwords)
        a->nwords = w + 1;
}

/* argument of an action primitive: number | parameter | field */
enum { ARG_NUM, ARG_PARAM, ARG_FIELD };
typedef struct { int kind; uint64_t num; int pidx; int fid; } parg_t;

static parg_t parse_arg(p4c_t *c, const rmt_action_t *a)
{
    parg_t r = { ARG_NUM, 0, 0, -1 };
    if (c->tt == TK_NUM) {
        r.num = expect_num(c);
        return r;
    }
    if (c->tt != TK_ID)
        perr(c, "bad primitive argument");
    /* parameter name? */
    for (int i = 0; i < a->nparams; i++) {
        if (!strcmp(a->pnames[i], c->text)) {
            r.kind = ARG_PARAM;
            r.pidx = i;
            next(c);
            return r;
        }
    }
    r.kind = ARG_FIELD;
    r.fid = expect_fieldref(c);
    return r;
}

static alu_insn_t insn0(alu_op_t op)
{
    alu_insn_t in;
    memset(&in, 0, sizeof(in));
    in.op = op;
    in.fd = in.fs1 = in.fs2 = in.fbase = -1;
    return in;
}

/* cache extern instance by name (declared via "cache name(size);") */
static int cache_find(p4c_t *c, const char *name)
{
    for (int i = 0; i < c->pipe->ncaches; i++)
        if (!strcmp(rmt_cache_name(c->pipe->caches[i]), name))
            return i;
    perr(c, "unknown cache extern");
    return -1;
}

/* dnn extern instance by name (declared via "dnn name(fields...);") */
static int dnn_find(p4c_t *c, const char *name)
{
    for (int i = 0; i < c->pipe->ndnns; i++)
        if (!strcmp(rmt_dnn_name(c->pipe->dnns[i]), name))
            return i;
    perr(c, "unknown dnn extern");
    return -1;
}

/* sketch extern instance by name (declared via "sketch name(d,w,fields...);") */
static int sketch_find(p4c_t *c, const char *name)
{
    for (int i = 0; i < c->pipe->nsketches; i++)
        if (!strcmp(rmt_sketch_name(c->pipe->sketches[i]), name))
            return i;
    perr(c, "unknown sketch extern");
    return -1;
}

/* resolve a metadata field an extern primitive implicitly accesses */
static int need_meta(p4c_t *c, const char *fullname)
{
    int fid = rmt_field_id(c->pipe, fullname);
    if (fid < 0)
        perr(c, "cache primitive needs a metadata field not declared");
    return fid;
}

static void parse_action(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    if (p->nactions >= RMT_MAX_ACTIONS)
        perr(c, "too many actions");
    rmt_action_t *a = &p->actions[p->nactions++];
    memset(a, 0, sizeof(*a));

    expect_id(c, a->name, sizeof(a->name));
    expect_punct(c, '(');
    while (!is_punct(c, ')')) {
        if (a->nparams >= RMT_MAX_PARAMS)
            perr(c, "too many action parameters");
        expect_id(c, a->pnames[a->nparams++], RMT_NAME_LEN);
        if (is_punct(c, ','))
            next(c);
    }
    next(c); /* ')' */
    expect_punct(c, '{');

    while (!is_punct(c, '}')) {
        char prim[RMT_NAME_LEN];
        expect_id(c, prim, sizeof(prim));

        if (!strcmp(prim, "add_header") || !strcmp(prim, "remove_header")) {
            expect_punct(c, '(');
            char h[RMT_NAME_LEN + 16];
            expect_hdrref(c, h, sizeof(h));
            expect_punct(c, ')');
            expect_punct(c, ';');
            int hid = rmt_hdr_id(p, h);
            if (hid < 0)
                perr(c, "unknown header");
            alu_insn_t in = insn0(prim[0] == 'a' ? OP_VALID : OP_INVALID);
            in.fd = (int16_t)hid;
            sched(c, a, in);
            continue;
        }

        /* in-network cache extern primitives: prim(cache, key_field) */
        {
            alu_op_t xop = OP_NOP;
            if (!strcmp(prim, "cs_lookup"))        xop = OP_CS_LOOKUP;
            else if (!strcmp(prim, "cs_insert"))   xop = OP_CS_INSERT;
            else if (!strcmp(prim, "pit_insert"))  xop = OP_PIT_INSERT;
            else if (!strcmp(prim, "pit_consume")) xop = OP_PIT_CONSUME;
            else if (!strcmp(prim, "util_mark"))   xop = OP_UTIL_MARK;
            else if (!strcmp(prim, "util_probe"))  xop = OP_UTIL_PROBE;
            if (xop != OP_NOP) {
                expect_punct(c, '(');
                char cn[RMT_NAME_LEN];
                expect_id(c, cn, sizeof(cn));
                expect_punct(c, ',');
                int keyf = expect_fieldref(c);
                expect_punct(c, ')');
                expect_punct(c, ';');

                alu_insn_t in = insn0(xop);
                in.imm = cache_find(c, cn);
                in.fs1 = (int16_t)keyf;
                switch (xop) {          /* implicit metadata operands */
                case OP_CS_LOOKUP:
                    in.fd = (int16_t)need_meta(c, "meta.cs_hit");
                    break;
                case OP_PIT_INSERT:
                    in.fd  = (int16_t)need_meta(c, "meta.drop");
                    in.fs2 = (int16_t)need_meta(c, "meta.ingress_port");
                    break;
                case OP_PIT_CONSUME:
                    in.fd = (int16_t)need_meta(c, "meta.egress_port");
                    break;
                case OP_UTIL_PROBE:
                    in.fd = (int16_t)need_meta(c, "meta.used");
                    break;
                default:        /* CS_INSERT / UTIL_MARK: no PHV write */
                    break;
                }
                sched(c, a, in);
                continue;
            }
        }

        /* neural inference extern: dnn_infer(net, dst_class_field) */
        if (!strcmp(prim, "dnn_infer")) {
            expect_punct(c, '(');
            char dn[RMT_NAME_LEN];
            expect_id(c, dn, sizeof(dn));
            expect_punct(c, ',');
            int dst = expect_fieldref(c);
            expect_punct(c, ')');
            expect_punct(c, ';');

            alu_insn_t in = insn0(OP_DNN_INFER);
            in.imm = dnn_find(c, dn);
            in.fd = (int16_t)dst;
            /* Order after writes to the first two feature fields; the
             * remaining features must be ready before the action.      */
            in.fs1 = (int16_t)rmt_dnn_input_fid(
                c->pipe->dnns[in.imm], 0);
            if (rmt_dnn_num_inputs(c->pipe->dnns[in.imm]) > 1)
                in.fs2 = (int16_t)rmt_dnn_input_fid(
                    c->pipe->dnns[in.imm], 1);
            sched(c, a, in);
            continue;
        }

        /* Count-Min Sketch extern:
         *   sketch_inc(name);           -- atomically increment d counters
         *   sketch_get(name, dst_field); -- fd = min of d counters       */
        if (!strcmp(prim, "sketch_inc") || !strcmp(prim, "sketch_get")) {
            int is_get = (prim[7] == 'g');   /* sketch_get vs sketch_inc */
            expect_punct(c, '(');
            char sn[RMT_NAME_LEN];
            expect_id(c, sn, sizeof(sn));
            int sidx = sketch_find(c, sn);
            alu_insn_t in = insn0(is_get ? OP_SKETCH_GET : OP_SKETCH_INC);
            in.imm = sidx;
            /* The extern reads the flow-key fields bound at the sketch
             * declaration, which the generic operand analysis cannot see.
             * Expose the first two keys as fs1/fs2 so sched() orders the
             * extern after writes to them (VLIW reads old values, so an
             * unencoded dependency would count a stale key). Remaining
             * key fields must already be ready before the action.      */
            if (sidx >= 0 && sidx < c->pipe->nsketches) {
                const rmt_sketch_t *sk = c->pipe->sketches[sidx];
                if (sk->nkeys > 0) in.fs1 = (int16_t)sk->key_fids[0];
                if (sk->nkeys > 1) in.fs2 = (int16_t)sk->key_fids[1];
            }
            if (is_get) {
                expect_punct(c, ',');
                in.fd = (int16_t)expect_fieldref(c);
            } else {
                in.fd = -1;    /* no PHV write for inc (fire-and-forget) */
            }
            expect_punct(c, ')');
            expect_punct(c, ';');
            sched(c, a, in);
            continue;
        }

        if (!strcmp(prim, "copy_indexed")) {
            /* copy_indexed(dst, stack.field, index_field) */
            expect_punct(c, '(');
            int dst = expect_fieldref(c);
            expect_punct(c, ',');
            char an[RMT_NAME_LEN], fn[RMT_NAME_LEN];
            expect_id(c, an, sizeof(an));
            expect_punct(c, '.');
            expect_id(c, fn, sizeof(fn));
            expect_punct(c, ',');
            int idxf = expect_fieldref(c);
            expect_punct(c, ')');
            expect_punct(c, ';');

            int ai = rmt_arr_id(p, an);
            if (ai < 0)
                perr(c, "copy_indexed: unknown header stack");
            char full[4 * RMT_NAME_LEN];
            snprintf(full, sizeof(full), "%s[0].%s", an, fn);
            int fbase = rmt_field_id(p, full);
            if (fbase < 0)
                perr(c, "copy_indexed: unknown stack field");

            alu_insn_t in = insn0(OP_MOVX);
            in.fd = (int16_t)dst;
            in.fs1 = (int16_t)idxf;
            in.fbase = (int16_t)fbase;
            in.stride = (int16_t)p->hdefs[p->adefs[ai].first_hdr].nfields;
            in.imm = p->adefs[ai].count;   /* bound */
            sched(c, a, in);
            continue;
        }

        /* set/add/sub/and/or/xor/shl/shr(dst, arg) */
        static const struct { const char *n; alu_op_t f, i; } binops[] = {
            { "add", OP_ADD, OP_ADDI }, { "sub", OP_SUB, OP_SUBI },
            { "and", OP_AND, OP_ANDI }, { "or",  OP_OR,  OP_ORI  },
            { "xor", OP_XOR, OP_XORI }, { "shl", OP_SHL, OP_SHLI },
            { "shr", OP_SHR, OP_SHRI },
        };

        expect_punct(c, '(');
        int dst = expect_fieldref(c);
        expect_punct(c, ',');
        parg_t arg = parse_arg(c, a);
        expect_punct(c, ')');
        expect_punct(c, ';');

        alu_insn_t in;
        if (!strcmp(prim, "set")) {
            if (arg.kind == ARG_FIELD) {
                in = insn0(OP_MOV);
                in.fs1 = (int16_t)arg.fid;
            } else {
                in = insn0(OP_MOVI);
                if (arg.kind == ARG_PARAM) {
                    in.use_param = 1;
                    in.pidx = (uint8_t)arg.pidx;
                } else {
                    in.imm = (int64_t)arg.num;
                }
            }
            in.fd = (int16_t)dst;
            sched(c, a, in);
            continue;
        }

        const alu_op_t *fop = NULL;
        alu_op_t fi = OP_NOP, ii = OP_NOP;
        for (size_t k = 0; k < sizeof(binops) / sizeof(binops[0]); k++) {
            if (!strcmp(prim, binops[k].n)) {
                fi = binops[k].f;
                ii = binops[k].i;
                fop = &fi;
                break;
            }
        }
        if (!fop)
            perr(c, "unknown action primitive");

        if (arg.kind == ARG_FIELD) {
            in = insn0(fi);
            in.fs1 = (int16_t)dst;
            in.fs2 = (int16_t)arg.fid;
        } else {
            in = insn0(ii);
            in.fs1 = (int16_t)dst;
            if (arg.kind == ARG_PARAM) {
                in.use_param = 1;
                in.pidx = (uint8_t)arg.pidx;
            } else {
                in.imm = (int64_t)arg.num;
            }
        }
        in.fd = (int16_t)dst;
        sched(c, a, in);
    }
    next(c); /* '}' */
}

/* ------------------------------------------------------------------ */
/* tables                                                             */
/* ------------------------------------------------------------------ */
static int action_find(p4c_t *c, const char *name)
{
    for (int i = 0; i < c->pipe->nactions; i++)
        if (!strcmp(c->pipe->actions[i].name, name))
            return i;
    perr(c, "unknown action");
    return -1;
}

static void parse_table(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    if (p->ntables >= RMT_MAX_TABLES)
        perr(c, "too many tables");
    rmt_table_t *t = &p->tables[p->ntables++];
    memset(t, 0, sizeof(*t));
    t->default_action = -1;
    t->stage = -1;
    t->pred_hdr = -1;

    expect_id(c, t->name, sizeof(t->name));
    expect_punct(c, '{');

    while (!is_punct(c, '}')) {
        if (accept_kw(c, "key")) {
            expect_punct(c, '{');
            while (!is_punct(c, '}')) {
                if (t->nkeys >= RMT_MAX_KEYS)
                    perr(c, "too many key fields");
                int fid = expect_fieldref(c);
                expect_punct(c, ':');
                int mt;
                if (accept_kw(c, "exact"))
                    mt = MATCH_EXACT;
                else if (accept_kw(c, "lpm"))
                    mt = MATCH_LPM;
                else {
                    perr(c, "match type must be exact|lpm");
                    return;
                }
                expect_punct(c, ';');
                t->keys[t->nkeys].field_id = fid;
                t->keys[t->nkeys].mtype = mt;
                t->nkeys++;
            }
            next(c);
        } else if (accept_kw(c, "actions")) {
            expect_punct(c, '{');
            while (!is_punct(c, '}')) {
                char an[RMT_NAME_LEN];
                expect_id(c, an, sizeof(an));
                expect_punct(c, ';');
                if (t->nactions >= RMT_MAX_ACTIONS)
                    perr(c, "too many table actions");
                t->action_ids[t->nactions++] = action_find(c, an);
            }
            next(c);
        } else if (accept_kw(c, "default_action")) {
            char an[RMT_NAME_LEN];
            expect_id(c, an, sizeof(an));
            t->default_action = action_find(c, an);
            if (is_punct(c, '(')) {
                next(c);
                int np = 0;
                while (!is_punct(c, ')')) {
                    if (np >= RMT_MAX_PARAMS)
                        perr(c, "too many default params");
                    t->def_params[np++] = expect_num(c);
                    if (is_punct(c, ','))
                        next(c);
                }
                next(c);
            }
            expect_punct(c, ';');
        } else {
            perr(c, "unexpected token in table");
        }
    }
    next(c); /* '}' */
}

/* ------------------------------------------------------------------ */
/* control ingress -> stage assignment                                */
/* ------------------------------------------------------------------ */
static rmt_table_t *table_find(p4c_t *c, const char *name)
{
    for (int i = 0; i < c->pipe->ntables; i++)
        if (!strcmp(c->pipe->tables[i].name, name))
            return &c->pipe->tables[i];
    perr(c, "unknown table in apply()");
    return NULL;
}

static void parse_apply(p4c_t *c, int pred_hdr)
{
    expect_punct(c, '(');
    char tn[RMT_NAME_LEN];
    expect_id(c, tn, sizeof(tn));
    expect_punct(c, ')');
    expect_punct(c, ';');
    rmt_table_t *t = table_find(c, tn);
    if (t->stage >= 0)
        perr(c, "table applied more than once");
    if (c->next_stage >= RMT_MAX_STAGES)
        perr(c, "control flow needs more stages than available");
    t->stage = c->next_stage++;
    t->pred_hdr = pred_hdr;
}

static void parse_control(p4c_t *c)
{
    char name[RMT_NAME_LEN];
    expect_id(c, name, sizeof(name));  /* e.g. "ingress" */
    expect_punct(c, '{');
    while (!is_punct(c, '}')) {
        if (accept_kw(c, "apply")) {
            parse_apply(c, -1);
        } else if (accept_kw(c, "if")) {
            expect_punct(c, '(');
            if (!accept_kw(c, "valid"))
                perr(c, "only valid(hdr) conditions are supported");
            expect_punct(c, '(');
            char h[RMT_NAME_LEN + 16];
            expect_hdrref(c, h, sizeof(h));
            expect_punct(c, ')');
            expect_punct(c, ')');
            int hid = rmt_hdr_id(c->pipe, h);
            if (hid < 0)
                perr(c, "unknown header in valid()");
            expect_punct(c, '{');
            while (!is_punct(c, '}')) {
                if (!accept_kw(c, "apply"))
                    perr(c, "expected apply() in if block");
                parse_apply(c, hid);
            }
            next(c);
        } else {
            perr(c, "unexpected token in control");
        }
    }
    next(c);

    if (c->next_stage > c->pipe->nstages)
        c->pipe->nstages = c->next_stage;
}

/* top-level: cache <name>(<slots>);  -- in-network cache extern     */
static void parse_cache(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    if (p->ncaches >= RMT_MAX_CACHES)
        perr(c, "too many cache externs");
    char name[RMT_NAME_LEN];
    expect_id(c, name, sizeof(name));
    expect_punct(c, '(');
    int nslots = (int)expect_num(c);
    expect_punct(c, ')');
    expect_punct(c, ';');
    p->caches[p->ncaches++] = rmt_cache_create(name, nslots);
}

/* dnn <name>(<field>, <field>, ...);  -- inference extern with its
 * input feature fields bound at declaration */
static void parse_dnn(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    if (p->ndnns >= RMT_MAX_DNNS)
        perr(c, "too many dnn externs");
    char name[RMT_NAME_LEN];
    expect_id(c, name, sizeof(name));
    expect_punct(c, '(');
    int fids[RMT_DNN_MAX_IN], nin = 0;
    while (!is_punct(c, ')')) {
        if (nin >= RMT_DNN_MAX_IN)
            perr(c, "too many dnn inputs");
        fids[nin++] = expect_fieldref(c);
        if (is_punct(c, ','))
            next(c);
    }
    next(c); /* ')' */
    expect_punct(c, ';');
    rmt_dnn_t *d = rmt_dnn_create(name, nin, fids);
    if (!d)
        perr(c, "bad dnn declaration");
    p->dnns[p->ndnns++] = d;
}

/* sketch <name>(<depth>, <width>, <field>, <field>, ...);
 * Count-Min Sketch extern for flow frequency estimation.
 * depth = number of hash functions (rows), width = counters per row.
 * Following fields are the flow-key (bound at declaration).         */
static void parse_sketch(p4c_t *c)
{
    rmt_pipeline_t *p = c->pipe;
    if (p->nsketches >= RMT_MAX_SKETCHES)
        perr(c, "too many sketch externs");
    char name[RMT_NAME_LEN];
    expect_id(c, name, sizeof(name));
    expect_punct(c, '(');
    int d = (int)expect_num(c);
    expect_punct(c, ',');
    int w = (int)expect_num(c);
    int fids[RMT_DNN_MAX_IN], nin = 0;
    while (!is_punct(c, ')')) {
        expect_punct(c, ',');
        if (nin >= RMT_DNN_MAX_IN)
            perr(c, "too many sketch key fields");
        fids[nin++] = expect_fieldref(c);
    }
    next(c); /* ')' */
    expect_punct(c, ';');
    rmt_sketch_t *s = rmt_sketch_create(name, d, w, nin, fids);
    if (!s)
        perr(c, "bad sketch declaration");
    p->sketches[p->nsketches++] = s;
}

/* ------------------------------------------------------------------ */
/* driver                                                             */
/* ------------------------------------------------------------------ */
int p4c_compile_file(rmt_pipeline_t *p, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "p4c: cannot open %s\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        fprintf(stderr, "p4c: cannot determine size of %s\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_SET);

    p4c_t c;
    memset(&c, 0, sizeof(c));
    c.src = (char *)malloc((size_t)sz + 1);
    if (!c.src) { fclose(fp); return -1; }
    c.len = fread(c.src, 1, (size_t)sz, fp);
    c.src[c.len] = 0;
    fclose(fp);
    c.line = 1;
    c.pipe = p;
    c.next_stage = 0;

    if (setjmp(c.err)) {
        /* cleanup: destroy any caches/dnns/sketches/tables allocated before the error */
        for (int i = 0; i < p->ncaches; i++)
            rmt_cache_destroy(p->caches[i]);
        for (int i = 0; i < p->ndnns; i++)
            rmt_dnn_destroy(p->dnns[i]);
        for (int i = 0; i < p->nsketches; i++)
            rmt_sketch_destroy(p->sketches[i]);
        for (int i = 0; i < p->ntables; i++)
            rmt_table_free(&p->tables[i]);
        p->ncaches = p->ndnns = p->nsketches = p->ntables = 0;
        free(c.src);
        return -1;
    }

    next(&c);
    while (c.tt != TK_EOF) {
        if (accept_kw(&c, "header"))
            parse_header(&c, false);
        else if (accept_kw(&c, "metadata"))
            parse_header(&c, true);
        else if (accept_kw(&c, "parser"))
            parse_parser(&c);
        else if (accept_kw(&c, "cache"))
            parse_cache(&c);
        else if (accept_kw(&c, "dnn"))
            parse_dnn(&c);
        else if (accept_kw(&c, "sketch"))
            parse_sketch(&c);
        else if (accept_kw(&c, "action"))
            parse_action(&c);
        else if (accept_kw(&c, "table"))
            parse_table(&c);
        else if (accept_kw(&c, "control"))
            parse_control(&c);
        else
            perr(&c, "expected top-level declaration");
    }

    free(c.src);
    rmt_finalize(p);

    printf("p4c: compiled %s: %d headers, %d fields, %d parser states, "
           "%d actions, %d tables, %d stages\n",
           path, p->nhdrs, p->nfields, p->nstates, p->nactions,
           p->ntables, p->nstages);
    return 0;
}
