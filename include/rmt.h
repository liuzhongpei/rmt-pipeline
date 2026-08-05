/*
 * rmt.h - Reconfigurable Match-Action Tables (RMT) programmable pipeline
 *
 * Architecture (software model of an RMT switch pipeline):
 *
 *   pkt -> [Programmable Parser] -> PHV -> [Stage0] -> [Stage1] -> ... -> [StageN] -> [Deparser] -> pkt
 *
 *   Each stage = Match (tree-structured lookup engine, 5-tuple / LPM / exact)
 *              + Action (16 RISC ALUs executing one VLIW word per cycle, in parallel)
 *
 *   The whole pipeline (parser graph, tables, actions/ALU microcode, control
 *   flow) is configured by the P4 compiler (p4c.c) from a P4 source program.
 */
#ifndef RMT_H
#define RMT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Performance hints & portability                                     */
/* ------------------------------------------------------------------ */
#ifdef __GNUC__
#define RMT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define RMT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define RMT_PREFETCH(p) __builtin_prefetch(p, 0, 1)
#define RMT_HOT         __attribute__((hot))
#define RMT_ALIGNED(n)  __attribute__((aligned(n)))
#else
#define RMT_LIKELY(x)   (x)
#define RMT_UNLIKELY(x) (x)
#define RMT_PREFETCH(p) ((void)0)
#define RMT_HOT
#define RMT_ALIGNED(n)
#endif

#define RMT_MAX_FIELDS   160    /* total PHV fields                    */
#define RMT_MAX_HDRS     40     /* header instances                    */
#define RMT_MAX_ARRAYS   8      /* header stacks (e.g. SRv6 seg list)  */
#define RMT_MAX_STATES   32     /* parser states                       */
#define RMT_MAX_TABLES   16
#define RMT_MAX_ACTIONS  32
#define RMT_MAX_STAGES   8      /* physical stages (>= 4 used)         */
#define RMT_MIN_STAGES   4
#define RMT_NUM_ALUS     16     /* ALUs per stage == VLIW width        */
#define RMT_MAX_WORDS    8      /* max VLIW words per action program   */
#define RMT_MAX_KEYS     8      /* max key fields per table (5-tuple)  */
#define RMT_MAX_PARAMS   8      /* action data words per entry         */
#define RMT_MAX_CACHES   4      /* stateful cache externs per pipeline */
#define RMT_MAX_DNNS     2      /* neural inference externs per pipeline */
#define RMT_DNN_MAX_IN   8      /* input features                      */
#define RMT_DNN_MAX_HID  32     /* hidden neurons                      */
#define RMT_DNN_MAX_OUT  4      /* output classes                      */
#define RMT_NAME_LEN     48
#define RMT_PKT_MAX      2048
#define RMT_BATCH_MAX    64     /* max packets per batch call           */

/* ------------------------------------------------------------------ */
/* PHV: Packet Header Vector                                          */
/* A field value is a 128-bit big-endian integer, right aligned in v. */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  v[16];
    uint16_t bitlen;
} fv_t;

/* header validity: uint64_t bitmap (supports up to 64 headers,
 * RMT_MAX_HDRS <= 64).  Single bit-test vs bool array = fewer
 * cache lines touched per packet.                                    */
_Static_assert(RMT_MAX_HDRS <= 64, "hvalid bitmap needs <= 64 headers");

typedef struct {
    fv_t     fields[RMT_MAX_FIELDS];
    uint64_t hvalid;                    /* bit h = header h valid      */

    /* packet-side context for stateful externs (in-network cache):
     * pl/pl_len expose the input payload (e.g. NDN Data content) to
     * cs_insert; a cs_lookup hit fills inject[] and the deparser
     * appends it instead of the original payload (cache reply).
     *
     * inject_buf is a pointer to an externally-owned buffer (thread-
     * local or caller-provided), NOT part of this struct. This keeps
     * phv_t ~3KB instead of ~5KB on stack, reducing L1 pressure.    */
    const uint8_t *pl;
    int            pl_len;
    uint8_t       *inject;              /* -> external buffer (2KB)    */
    int            inject_len;
    bool           has_inject;
} phv_t;

/* inline helpers for the bitmap-based hvalid                          */
static inline bool phv_hdr_valid(const phv_t *phv, int h)
{
    return (phv->hvalid >> h) & 1;
}
static inline void phv_hdr_set(phv_t *phv, int h)
{
    phv->hvalid |= (1ULL << h);
}
static inline void phv_hdr_clr(phv_t *phv, int h)
{
    phv->hvalid &= ~(1ULL << h);
}

/* ------------------------------------------------------------------ */
/* Symbol tables (built by the P4 compiler)                           */
/* ------------------------------------------------------------------ */
typedef struct {                       /* full name e.g. "ipv6.dst"   */
    char     name[RMT_NAME_LEN];
    int      hdr;
    uint16_t bitlen;
} field_def_t;

typedef struct {
    char name[RMT_NAME_LEN];
    int  first_field, nfields;
    int  bitlen;                       /* total header bits           */
    bool is_meta;                      /* metadata: never deparsed    */
} hdr_def_t;

typedef struct {                       /* header stack "seg[N]"       */
    char name[RMT_NAME_LEN];
    int  first_hdr, count;
} arr_def_t;

/* ------------------------------------------------------------------ */
/* Programmable parser (parse graph)                                  */
/* ------------------------------------------------------------------ */
enum { POP_EXTRACT, POP_EXTRACT_ARRAY };

typedef struct {
    int kind;
    int hdr;                           /* header (or first of array)  */
    int count_field;                   /* POP_EXTRACT_ARRAY: count =  */
    int count_add;                     /*   phv[count_field]+count_add*/
} parse_op_t;

typedef struct { uint64_t val; int next; } sel_case_t;

#define PS_ACCEPT (-1)

typedef struct {
    char        name[RMT_NAME_LEN];
    parse_op_t  ops[4];
    int         nops;
    int         sel_field;             /* -1: unconditional def_next  */
    sel_case_t  cases[16];
    int         ncases;
    int         def_next;
} parse_state_t;

/* ------------------------------------------------------------------ */
/* RISC ALU: 3-address register/PHV instruction set                   */
/* The PHV acts as the register file shared by the 16 ALUs of a stage.*/
/* ------------------------------------------------------------------ */
typedef enum {
    OP_NOP = 0,
    OP_MOVI,                           /* fd = imm                    */
    OP_MOV,                            /* fd = fs1 (full field width) */
    OP_ADD, OP_SUB, OP_AND, OP_OR,     /* fd = fs1 op fs2             */
    OP_XOR, OP_SHL, OP_SHR,
    OP_ADDI, OP_SUBI, OP_ANDI, OP_ORI, /* fd = fs1 op imm             */
    OP_XORI, OP_SHLI, OP_SHRI,
    OP_MOVX,                           /* fd = phv[fbase+idx*stride], */
                                       /*   idx = phv[fs1], bound=imm */
    OP_VALID, OP_INVALID,              /* header validity (fd = hdr)  */

    /* stateful extern ops: in-network cache engine (imm = cache id,
     * fs1 = key/name field). Used to build NDN CS + PIT.            */
    OP_CS_LOOKUP,                      /* hit: fd(cs_hit)=1 + inject  */
    OP_CS_INSERT,                      /* store packet payload @ key  */
    OP_PIT_INSERT,                     /* key -> fs2(ingress port);   */
                                       /*   dup: fd(drop)=1 (aggreg.) */
    OP_PIT_CONSUME,                    /* hit: fd(egress)=port, del;  */
                                       /*   miss: meta.drop=1         */

    /* address-utilization ops on the same cache engine (imm = cache
     * id, fs1 = address field). They add epoch-based aging and back
     * the dynamic network telescope (MORP4-style): outbound traffic
     * marks its source as used, inbound traffic probes whether the
     * destination is currently used.                                */
    OP_UTIL_MARK,                      /* key := used @ current epoch */
    OP_UTIL_PROBE,                     /* live hit: fd(used)=1;       */
                                       /*   miss/expired: no write    */

    /* neural inference extern (imm = dnn id): gather the feature
     * fields bound at declaration, run the quantized MLP, write the
     * argmax class to fd. Used for anomaly traffic detection.       */
    OP_DNN_INFER
} alu_op_t;

typedef struct {
    alu_op_t op;
    int16_t  fd, fs1, fs2;             /* PHV field ids               */
    int16_t  fbase, stride;            /* OP_MOVX only                */
    int64_t  imm;
    uint8_t  use_param, pidx;          /* imm taken from action data  */
} alu_insn_t;

typedef struct {
    char       name[RMT_NAME_LEN];
    int        nparams;
    char       pnames[RMT_MAX_PARAMS][RMT_NAME_LEN];
    alu_insn_t prog[RMT_MAX_WORDS][RMT_NUM_ALUS];   /* VLIW microcode */
    int        nslots[RMT_MAX_WORDS];
    int        nwords;
} rmt_action_t;

/* ------------------------------------------------------------------ */
/* Tree-structured lookup engine                                      */
/* One binary (bit) trie per key field, chained per field: matching   */
/* walks field-0's tree, then descends into field-1's tree, etc.,     */
/* with longest-prefix-first backtracking. Supports exact/LPM per     */
/* field, which covers 5-tuple classification.                        */
/* ------------------------------------------------------------------ */
enum { MATCH_EXACT, MATCH_LPM };

typedef struct trie_node {
    struct trie_node *b[2];
    struct trie_node *next_root;       /* next key field's tree       */
    struct rmt_entry *entry;           /* entries (last key field)    */
} trie_node_t;

typedef struct rmt_entry {
    uint8_t  key[RMT_MAX_KEYS][16];    /* big-endian, right aligned   */
    uint16_t plen[RMT_MAX_KEYS];       /* prefix length per field     */
    int      action_id;
    uint64_t params[RMT_MAX_PARAMS];
    int      nparams;
    int      id;
    struct rmt_entry *next;
} rmt_entry_t;

typedef struct {
    char name[RMT_NAME_LEN];
    struct { int field_id; int mtype; } keys[RMT_MAX_KEYS];
    int nkeys;
    int action_ids[RMT_MAX_ACTIONS];
    int nactions;
    trie_node_t *root;
    int      default_action;           /* -1: no-op on miss           */
    uint64_t def_params[RMT_MAX_PARAMS];
    int      stage;                    /* physical stage assignment   */
    int      pred_hdr;                 /* gate: apply iff hdr valid   */
    int      nentries;
} rmt_table_t;

/* ------------------------------------------------------------------ */
/* In-network cache engine (stateful extern)                          */
/* A dataplane-writable, name-keyed store attached to the pipeline;   */
/* instances are declared in P4 ("cache cs(64);") and accessed from   */
/* actions via cs_lookup/cs_insert/pit_insert/pit_consume primitives. */
/* Backs the NDN Content Store and Pending Interest Table.            */
/* ------------------------------------------------------------------ */
typedef struct rmt_cache rmt_cache_t;

/* ------------------------------------------------------------------ */
/* DNN inference engine (extern)                                      */
/* A per-pipeline quantized MLP (Q16.16 fixed point, integer-only     */
/* forward pass) modelling an inference block next to the ALUs.       */
/* Declared in P4 with its input fields bound:                        */
/*     dnn ids(ipv4.len, ipv4.ttl, tcp.flags, tcp.window);            */
/* and invoked from an action:  dnn_infer(ids, meta.class);           */
/* The control plane trains in float and installs the model with      */
/* rmt_dnn_load(), which quantizes the weights.                       */
/* ------------------------------------------------------------------ */
typedef struct rmt_dnn rmt_dnn_t;

/* ------------------------------------------------------------------ */
/* Pipeline                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    field_def_t   fdefs[RMT_MAX_FIELDS]; int nfields;
    hdr_def_t     hdefs[RMT_MAX_HDRS];   int nhdrs;
    arr_def_t     adefs[RMT_MAX_ARRAYS]; int narrs;

    parse_state_t states[RMT_MAX_STATES]; int nstates; int start_state;

    rmt_action_t  actions[RMT_MAX_ACTIONS]; int nactions;
    rmt_table_t   tables[RMT_MAX_TABLES];   int ntables;
    int           nstages;

    /* per-stage table index lists, built by rmt_finalize(): avoids
     * scanning every table at every stage on the per-packet fast path */
    int8_t stage_tbl[RMT_MAX_STAGES][RMT_MAX_TABLES];
    int    nstage_tbl[RMT_MAX_STAGES];

    /* pristine PHV field template (zero values, bitlens preset),
     * built by rmt_finalize(); per-packet reset is a single memcpy */
    fv_t   fields_tpl[RMT_MAX_FIELDS];

    int  fid_drop, fid_eport;          /* cached meta.drop/egress_port*/
    int  fid_iport, fid_ts;            /* intrinsic metadata (telemetry):
                                          meta.ingress_port, meta.timestamp
                                          set by the pipeline at ingress */
    uint64_t meta_valid_mask;           /* precomputed: bits for metadata
                                          headers (always valid per pkt) */
    rmt_cache_t *caches[RMT_MAX_CACHES];  /* stateful cache externs   */
    int          ncaches;
    rmt_dnn_t   *dnns[RMT_MAX_DNNS];      /* DNN inference externs    */
    int          ndnns;
    bool verbose;
} rmt_pipeline_t;

typedef struct {
    uint8_t data[RMT_PKT_MAX];
    int     len;
    int     in_port, out_port;
    bool    dropped;
} rmt_pkt_t;

/* ---- batch processing for high throughput (amortizes overhead) ---- */
int  rmt_process_batch(rmt_pipeline_t * restrict p,
                       const rmt_pkt_t * restrict in,
                       rmt_pkt_t * restrict out, int count);
/* returns 0 on success; processes up to RMT_BATCH_MAX packets;
 * single clock_gettime per batch; no per-packet syscall overhead. */

/* pointer-array variant: zero-copy burst from lock-free rings
 * (packets need not be contiguous in memory).                       */
int  rmt_process_burst(rmt_pipeline_t * restrict p,
                       rmt_pkt_t * const * restrict in,
                       rmt_pkt_t * restrict out, int count);

/* ------------------------------------------------------------------ */
/* mq.c: multi-queue parallel processing (RSS + run-to-completion)    */
/*                                                                    */
/* Emulates a multi-queue NIC: the dispatcher RSS-hashes each packet  */
/* to one of N lock-free SPSC rings; each ring is drained by a        */
/* dedicated worker thread pinned to its own core, which processes    */
/* bursts of up to RMT_BATCH_MAX packets and invokes the user         */
/* callback in worker context (models TX).  Same flow -> same hash    */
/* -> same queue: per-flow packet order is preserved lock-free.       */
/* ------------------------------------------------------------------ */
typedef struct rmt_mq rmt_mq_t;

/* called in worker context after each packet; use per-worker state
 * in arg (indexed by worker id) to stay lock-free.                  */
typedef void (*rmt_mq_cb_t)(int worker, const rmt_pkt_t *in,
                            rmt_pkt_t *out, void *arg);

rmt_mq_t *rmt_mq_create(rmt_pipeline_t *p, int nworkers, int ring_order,
                        rmt_mq_cb_t cb, void *arg);
int      rmt_mq_start(rmt_mq_t *mq);            /* spawn+pin workers  */
bool     rmt_mq_submit(rmt_mq_t *mq, rmt_pkt_t *pkt, uint32_t hash);
void     rmt_mq_flush(rmt_mq_t *mq);            /* wait all processed */
void     rmt_mq_stop(rmt_mq_t *mq);             /* drain + join       */
void     rmt_mq_destroy(rmt_mq_t *mq);
uint64_t rmt_mq_worker_processed(const rmt_mq_t *mq, int worker);
uint64_t rmt_mq_worker_dropped(const rmt_mq_t *mq, int worker);
uint32_t rmt_rss_hash(const uint8_t *key, int len);  /* FNV-1a 32    */
uint32_t rmt_rss_flow_hash(const uint8_t *pkt, int len);
/* parses Eth/VLAN -> IPv4|IPv6(+ext hdrs incl. SRH) -> TCP|UDP and
 * hashes the 5-tuple: all packets of one flow map to one queue.    */

/* ---------------- phv (hot path, inlined) ---------------- */
/* These run millions of times per second (per-bit parse/lookup, ALU
 * operand access); defined inline so every module gets direct code. */

static inline void fv_clear(fv_t *f)
{
    memset(f->v, 0, sizeof(f->v));
}

/* bit i of the bitlen-wide value, i=0 is the MSB */
static inline int fv_get_bit(const fv_t *f, int i)
{
    int p = 128 - f->bitlen + i;
    return (f->v[p >> 3] >> (7 - (p & 7))) & 1;
}

static inline void fv_set_bit(fv_t *f, int i, int b)
{
    int p = 128 - f->bitlen + i;
    uint8_t m = (uint8_t)(1u << (7 - (p & 7)));
    if (b) f->v[p >> 3] |= m;
    else   f->v[p >> 3] &= (uint8_t)~m;
}

static inline uint64_t fv_get_u64(const fv_t *f)
{
    uint64_t r;
    memcpy(&r, f->v + 8, 8);               /* low 64 bits, big endian */
    return __builtin_bswap64(r);
}

/* zero all bits above bitlen */
static inline void fv_mask(fv_t *f)
{
    int lead = 128 - f->bitlen;             /* leading zero bits      */
    memset(f->v, 0, (size_t)(lead >> 3));
    if (lead & 7)
        f->v[lead >> 3] &= (uint8_t)(0xffu >> (lead & 7));
}

static inline void fv_set_u64(fv_t *f, uint64_t x)
{
    uint64_t be = __builtin_bswap64(x);
    memset(f->v, 0, 8);
    memcpy(f->v + 8, &be, 8);
    fv_mask(f);
}

/* copy value, keep destination width (truncate / zero-extend) */
static inline void fv_copy(fv_t *d, const fv_t *s)
{
    memcpy(d->v, s->v, 16);
    fv_mask(d);
}

/* ---------------- phv.c ---------------- */
const char *fv_str(const fv_t *f, char *buf, size_t n);

/* ---------------- pipeline.c ---------------- */
rmt_pipeline_t *rmt_create(void);
void rmt_destroy(rmt_pipeline_t *p);
int  rmt_hdr_add(rmt_pipeline_t *p, const char *name, bool is_meta);
int  rmt_field_add(rmt_pipeline_t *p, int hdr, const char *fullname, int bitlen);
int  rmt_hdr_id(const rmt_pipeline_t *p, const char *name);
int  rmt_field_id(const rmt_pipeline_t *p, const char *fullname);
int  rmt_arr_id(const rmt_pipeline_t *p, const char *name);
void rmt_finalize(rmt_pipeline_t *p);
void rmt_phv_reset(const rmt_pipeline_t *p, phv_t *phv);
int  rmt_process(rmt_pipeline_t * restrict p,
                 const rmt_pkt_t * restrict in, rmt_pkt_t * restrict out);

/* ---------------- parser.c ---------------- */
int  rmt_parse(const rmt_pipeline_t *p, const uint8_t *pkt, int len,
               phv_t *phv, int *hdr_bytes);
int  rmt_deparse(const rmt_pipeline_t *p, const phv_t *phv,
                 uint8_t *out, int cap);

/* ---------------- match.c ---------------- */
rmt_entry_t *rmt_table_insert(rmt_pipeline_t *p, rmt_table_t *t,
                              const rmt_entry_t *tpl);
rmt_entry_t *rmt_table_lookup(const rmt_pipeline_t *p, const rmt_table_t *t,
                              const phv_t *phv);
void         rmt_table_free(rmt_table_t *t);

/* ---------------- alu.c ---------------- */
void rmt_action_exec(const rmt_pipeline_t *p, const rmt_action_t *a,
                     const uint64_t *params, phv_t *phv);

/* ---------------- cache.c ---------------- */
rmt_cache_t *rmt_cache_create(const char *name, int nslots);
void rmt_cache_destroy(rmt_cache_t *c);
const char *rmt_cache_name(const rmt_cache_t *c);
/* read (CS lookup); returns false on miss */
bool rmt_cache_get(rmt_cache_t *c, const fv_t *key,
                   uint8_t *data, int cap, int *len, uint64_t *aux);
/* write; only_if_absent: keep existing entry, return false (PIT add) */
bool rmt_cache_put(rmt_cache_t *c, const fv_t *key,
                   const uint8_t *data, int len, uint64_t aux,
                   bool only_if_absent);
/* read + delete (PIT consume); returns false on miss */
bool rmt_cache_take(rmt_cache_t *c, const fv_t *key, uint64_t *aux);
/* address-utilization view (dynamic network telescope):
 * entries marked with rmt_cache_mark() age out after ttl_epochs
 * control-plane ticks; rmt_cache_probe() is a non-destructive
 * liveness test. Aging is off (ttl = infinite) by default, so
 * cs/pit users are unaffected. */
void     rmt_cache_set_aging(rmt_cache_t *c, uint64_t ttl_epochs);
uint64_t rmt_cache_tick(rmt_cache_t *c);      /* epoch++, returns new */
void     rmt_cache_mark(rmt_cache_t *c, const fv_t *key);
bool     rmt_cache_probe(rmt_cache_t *c, const fv_t *key);
int      rmt_cache_live(rmt_cache_t *c);      /* non-expired entries  */
void rmt_cache_print_stats(const rmt_cache_t *c);

/* ---------------- dnn.c ---------------- */
rmt_dnn_t  *rmt_dnn_create(const char *name, int nin, const int *in_fids);
void        rmt_dnn_destroy(rmt_dnn_t *d);
const char *rmt_dnn_name(const rmt_dnn_t *d);
int         rmt_dnn_num_inputs(const rmt_dnn_t *d);
int         rmt_dnn_input_fid(const rmt_dnn_t *d, int i);
/* install a trained model; float weights are quantized to Q16.16.
 * w1[nhid][nin], b1[nhid], w2[nout][nhid], b2[nout];
 * scale[nin] = raw divisor per input (feature normalization). */
int  rmt_dnn_load(rmt_dnn_t *d, int nhid, int nout,
                  const float *w1, const float *b1,
                  const float *w2, const float *b2,
                  const uint32_t *scale);
/* integer-only forward pass over raw feature values -> argmax class */
int  rmt_dnn_infer(rmt_dnn_t *d, const uint64_t *x);
void rmt_dnn_print_stats(const rmt_dnn_t *d);

/* ---------------- p4c.c ---------------- */
int p4c_compile_file(rmt_pipeline_t *p, const char *path);

/* ---------------- runtime.c ---------------- */
int rmt_load_entries(rmt_pipeline_t *p, const char *path);

#endif /* RMT_H */
