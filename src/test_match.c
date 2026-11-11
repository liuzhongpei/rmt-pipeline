/*
 * test_match.c - unit tests for match table runtime insert/lookup/default API
 *
 * Exercises:
 *   1. Single-key exact match (insert + lookup hit/miss)
 *   2. Single-key LPM (longest prefix first)
 *   3. Multi-key (exact + lpm) cascaded trie lookup
 *   4. Default action on miss
 *   5. Multiple entries with same key (first-inserted wins)
 *   6. rmt_load_entries() file-based loading and lookup verification
 *   7. Edge cases: empty table, zero-length prefix (wildcard)
 *   8. Multi-field LPM cross-field backtracking (README §3.4)
 *   9. IPv6 LPM shared-prefix / split + insertion-order independence
 *
 * Build:  gcc -O2 -Wall -Wextra -std=c11 -pthread -Iinclude \
 *              src/test_match.c src/phv.c src/parser.c src/match.c src/alu.c \
 *              src/pipeline.c src/p4c.c src/runtime.c src/cache.c src/dnn.c \
 *              src/mq.c -o bin/test_match -lm
 * Run:    ./bin/test_match
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "rmt.h"

static int g_fails;

static void chk(const char *what, int ok)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fails++;
}

/* helper: set a PHV fv_t field from a u64 value with given bitlen */
static void set_field(fv_t *f, int bitlen, uint64_t val)
{
    memset(f, 0, sizeof(*f));
    f->bitlen = (uint16_t)bitlen;
    fv_set_u64(f, val);
}

/* helper: set a PHV fv_t field from IPv4 string */
static void set_field_ipv4(fv_t *f, const char *ip)
{
    memset(f, 0, sizeof(*f));
    f->bitlen = 32;
    struct in_addr a;
    inet_pton(AF_INET, ip, &a);
    uint32_t v = ntohl(a.s_addr);
    fv_set_u64(f, v);
}

/* helper: set a PHV fv_t field from IPv6 string */
static void set_field_ipv6(fv_t *f, const char *ip)
{
    memset(f, 0, sizeof(*f));
    f->bitlen = 128;
    struct in6_addr a;
    inet_pton(AF_INET6, ip, &a);
    memcpy(f->v, a.s6_addr, 16);
}

/* helper: fill entry key[kidx] from a u64 + prefix len */
static void entry_set_key_u64(rmt_entry_t *e, int kidx, int bitlen __attribute__((unused)),
                              uint64_t val, int plen)
{
    memset(e->key[kidx], 0, 16);
    uint64_t be = __builtin_bswap64(val);
    memcpy(e->key[kidx] + 8, &be, 8);
    /* right-align: clear leading bits beyond bitlen if needed */
    e->plen[kidx] = (uint16_t)plen;
}

/* helper: fill entry key[kidx] from IPv4 + prefix */
static void entry_set_key_ipv4(rmt_entry_t *e, int kidx,
                               const char *ip, int plen)
{
    memset(e->key[kidx], 0, 16);
    struct in_addr a;
    inet_pton(AF_INET, ip, &a);
    uint32_t v = ntohl(a.s_addr);
    e->key[kidx][12] = (uint8_t)(v >> 24);
    e->key[kidx][13] = (uint8_t)(v >> 16);
    e->key[kidx][14] = (uint8_t)(v >> 8);
    e->key[kidx][15] = (uint8_t)(v);
    e->plen[kidx] = (uint16_t)plen;
}

/* helper: fill entry key[kidx] from IPv6 + prefix (128-bit, fills key) */
static void entry_set_key_ipv6(rmt_entry_t *e, int kidx,
                               const char *ip, int plen)
{
    memset(e->key[kidx], 0, 16);
    struct in6_addr a;
    inet_pton(AF_INET6, ip, &a);
    memcpy(e->key[kidx], a.s6_addr, 16);
    e->plen[kidx] = (uint16_t)plen;
}

/* ================================================================ */
/* Test 1: Single-key exact match                                   */
/* ================================================================ */
static void test_exact_single(void)
{
    printf("\n---- Test 1: single-key exact match ----\n");

    rmt_pipeline_t *p = rmt_create();
    /* manually register a 16-bit field for the key */
    int hdr = rmt_hdr_add(p, "eth", false);
    int fid = rmt_field_add(p, hdr, "eth.type", 16);

    /* set up table with 1 exact key */
    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "ethertype_t");
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_EXACT;
    t->default_action = -1;
    p->ntables = 1;

    /* insert entry: ethertype=0x0800 -> action 1, param=5 */
    rmt_entry_t e1;
    memset(&e1, 0, sizeof(e1));
    entry_set_key_u64(&e1, 0, 16, 0x0800, 16);
    e1.action_id = 1;
    e1.params[0] = 5;
    e1.nparams = 1;
    rmt_entry_t *ins1 = rmt_table_insert(p, t, &e1);
    chk("insert exact entry returns non-NULL", ins1 != NULL);
    chk("entry id == 0 (first)", ins1 && ins1->id == 0);

    /* insert second entry: ethertype=0x86DD -> action 2 */
    rmt_entry_t e2;
    memset(&e2, 0, sizeof(e2));
    entry_set_key_u64(&e2, 0, 16, 0x86DD, 16);
    e2.action_id = 2;
    e2.nparams = 0;
    rmt_entry_t *ins2 = rmt_table_insert(p, t, &e2);
    chk("insert second exact entry", ins2 != NULL);

    /* lookup: hit 0x0800 */
    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    set_field(&phv.fields[fid], 16, 0x0800);
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("lookup 0x0800 hits", r != NULL);
    chk("lookup 0x0800 action_id == 1", r && r->action_id == 1);
    chk("lookup 0x0800 params[0] == 5", r && r->params[0] == 5);

    /* lookup: hit 0x86DD */
    set_field(&phv.fields[fid], 16, 0x86DD);
    r = rmt_table_lookup(p, t, &phv);
    chk("lookup 0x86DD hits", r != NULL);
    chk("lookup 0x86DD action_id == 2", r && r->action_id == 2);

    /* lookup: miss 0x8100 */
    set_field(&phv.fields[fid], 16, 0x8100);
    r = rmt_table_lookup(p, t, &phv);
    chk("lookup 0x8100 misses (NULL)", r == NULL);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 2: Single-key LPM (longest prefix first)                    */
/* ================================================================ */
static void test_lpm_single(void)
{
    printf("\n---- Test 2: single-key LPM ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int fid = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "route");
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_LPM;
    t->default_action = -1;
    p->ntables = 1;

    /* entries:
     *   10.0.0.0/8   -> action 0, param=1
     *   10.1.0.0/16  -> action 0, param=2
     *   10.1.2.0/24  -> action 0, param=3
     *   192.168.0.0/16 -> action 0, param=4
     */
    rmt_entry_t e; memset(&e, 0, sizeof(e));
    e.action_id = 0; e.nparams = 1;

    entry_set_key_ipv4(&e, 0, "10.0.0.0", 8);
    e.params[0] = 1;
    chk("insert 10.0.0.0/8", rmt_table_insert(p, t, &e) != NULL);

    entry_set_key_ipv4(&e, 0, "10.1.0.0", 16);
    e.params[0] = 2;
    chk("insert 10.1.0.0/16", rmt_table_insert(p, t, &e) != NULL);

    entry_set_key_ipv4(&e, 0, "10.1.2.0", 24);
    e.params[0] = 3;
    chk("insert 10.1.2.0/24", rmt_table_insert(p, t, &e) != NULL);

    entry_set_key_ipv4(&e, 0, "192.168.0.0", 16);
    e.params[0] = 4;
    chk("insert 192.168.0.0/16", rmt_table_insert(p, t, &e) != NULL);

    /* lookup: 10.1.2.3 -> should match /24 (param=3, most specific) */
    phv_t phv; memset(&phv, 0, sizeof(phv));
    set_field_ipv4(&phv.fields[fid], "10.1.2.3");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("10.1.2.3 matches /24 (longest)", r && r->params[0] == 3);

    /* lookup: 10.1.3.1 -> should match /16 (param=2) */
    set_field_ipv4(&phv.fields[fid], "10.1.3.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("10.1.3.1 matches /16", r && r->params[0] == 2);

    /* lookup: 10.2.0.1 -> should match /8 (param=1) */
    set_field_ipv4(&phv.fields[fid], "10.2.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("10.2.0.1 matches /8", r && r->params[0] == 1);

    /* lookup: 192.168.1.1 -> match /16 (param=4) */
    set_field_ipv4(&phv.fields[fid], "192.168.1.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("192.168.1.1 matches 192.168/16", r && r->params[0] == 4);

    /* lookup: 172.16.0.1 -> no match */
    set_field_ipv4(&phv.fields[fid], "172.16.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("172.16.0.1 misses (NULL)", r == NULL);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 3: Multi-key (exact + lpm) cascaded trie                    */
/* ================================================================ */
static void test_multi_key(void)
{
    printf("\n---- Test 3: multi-key (exact + lpm) ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int f_proto = rmt_field_add(p, hdr, "ipv4.proto", 8);
    int f_dst   = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "acl");
    t->nkeys = 2;
    t->keys[0].field_id = f_proto;
    t->keys[0].mtype = MATCH_EXACT;
    t->keys[1].field_id = f_dst;
    t->keys[1].mtype = MATCH_LPM;
    t->default_action = -1;
    p->ntables = 1;

    rmt_entry_t e; memset(&e, 0, sizeof(e));

    /* entry A: proto=6(TCP) + dst 10.0.0.0/8 -> action 1, param=10 */
    entry_set_key_u64(&e, 0, 8, 6, 8);
    entry_set_key_ipv4(&e, 1, "10.0.0.0", 8);
    e.action_id = 1; e.params[0] = 10; e.nparams = 1;
    chk("insert TCP+10/8", rmt_table_insert(p, t, &e) != NULL);

    /* entry B: proto=6(TCP) + dst 10.1.0.0/16 -> action 1, param=20 */
    entry_set_key_u64(&e, 0, 8, 6, 8);
    entry_set_key_ipv4(&e, 1, "10.1.0.0", 16);
    e.action_id = 1; e.params[0] = 20; e.nparams = 1;
    chk("insert TCP+10.1/16", rmt_table_insert(p, t, &e) != NULL);

    /* entry C: proto=17(UDP) + dst 10.0.0.0/8 -> action 2, param=30 */
    entry_set_key_u64(&e, 0, 8, 17, 8);
    entry_set_key_ipv4(&e, 1, "10.0.0.0", 8);
    e.action_id = 2; e.params[0] = 30; e.nparams = 1;
    chk("insert UDP+10/8", rmt_table_insert(p, t, &e) != NULL);

    phv_t phv; memset(&phv, 0, sizeof(phv));

    /* lookup: proto=6, dst=10.1.2.3 -> entry B (TCP, /16 more specific) */
    set_field(&phv.fields[f_proto], 8, 6);
    set_field_ipv4(&phv.fields[f_dst], "10.1.2.3");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("TCP 10.1.2.3 -> param=20 (TCP /16)", r && r->params[0] == 20);

    /* lookup: proto=6, dst=10.2.0.1 -> entry A (TCP, /8) */
    set_field_ipv4(&phv.fields[f_dst], "10.2.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("TCP 10.2.0.1 -> param=10 (TCP /8)", r && r->params[0] == 10);

    /* lookup: proto=17, dst=10.5.0.1 -> entry C (UDP, /8) */
    set_field(&phv.fields[f_proto], 8, 17);
    set_field_ipv4(&phv.fields[f_dst], "10.5.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("UDP 10.5.0.1 -> param=30 (UDP /8)", r && r->params[0] == 30);

    /* lookup: proto=1(ICMP), dst=10.1.2.3 -> miss (no proto=1 entries) */
    set_field(&phv.fields[f_proto], 8, 1);
    set_field_ipv4(&phv.fields[f_dst], "10.1.2.3");
    r = rmt_table_lookup(p, t, &phv);
    chk("ICMP 10.1.2.3 -> miss", r == NULL);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 4: Default action on miss                                   */
/* ================================================================ */
static void test_default_action(void)
{
    printf("\n---- Test 4: default action on miss ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int fid = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "route");
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_LPM;
    t->default_action = 99;         /* action id for default */
    t->def_params[0] = 42;
    p->ntables = 1;

    /* insert one entry */
    rmt_entry_t e; memset(&e, 0, sizeof(e));
    entry_set_key_ipv4(&e, 0, "10.0.0.0", 8);
    e.action_id = 1; e.params[0] = 1; e.nparams = 1;
    rmt_table_insert(p, t, &e);

    phv_t phv; memset(&phv, 0, sizeof(phv));

    /* hit */
    set_field_ipv4(&phv.fields[fid], "10.1.2.3");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("10.1.2.3 hits entry", r != NULL && r->action_id == 1);

    /* miss -> returns NULL, caller should use default_action */
    set_field_ipv4(&phv.fields[fid], "172.16.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("172.16.0.1 misses (NULL)", r == NULL);
    chk("default_action == 99", t->default_action == 99);
    chk("def_params[0] == 42", t->def_params[0] == 42);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 5: Duplicate key -> first-inserted wins                     */
/* ================================================================ */
static void test_first_insert_wins(void)
{
    printf("\n---- Test 5: duplicate key first-insert wins ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int fid = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "route");
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_EXACT;
    t->default_action = -1;
    p->ntables = 1;

    rmt_entry_t e; memset(&e, 0, sizeof(e));
    entry_set_key_ipv4(&e, 0, "10.0.0.1", 32);

    /* first insert: action=1 */
    e.action_id = 1; e.params[0] = 100; e.nparams = 1;
    rmt_table_insert(p, t, &e);

    /* second insert same key: action=2 */
    e.action_id = 2; e.params[0] = 200; e.nparams = 1;
    rmt_table_insert(p, t, &e);

    phv_t phv; memset(&phv, 0, sizeof(phv));
    set_field_ipv4(&phv.fields[fid], "10.0.0.1");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("lookup returns first-inserted (action=1)", r && r->action_id == 1);
    chk("lookup returns first-inserted (param=100)", r && r->params[0] == 100);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 6: rmt_load_entries() file-based loading                    */
/* ================================================================ */
static void test_load_entries(void)
{
    printf("\n---- Test 6: rmt_load_entries() file loading ----\n");

    /* Use the real srv6.p4 pipeline + entries file */
    rmt_pipeline_t *p = rmt_create();
    if (p4c_compile_file(p, "p4/srv6.p4") < 0) {
        chk("p4c compile srv6.p4", 0);
        rmt_destroy(p);
        return;
    }
    chk("p4c compile srv6.p4", 1);

    int rc = rmt_load_entries(p, "entries/srv6_entries.txt");
    chk("rmt_load_entries() returns 0", rc == 0);

    /* verify table was populated: local_sid should have at least 1 entry */
    rmt_table_t *t_local = NULL;
    for (int i = 0; i < p->ntables; i++) {
        if (!strcmp(p->tables[i].name, "local_sid")) {
            t_local = &p->tables[i];
            break;
        }
    }
    chk("found table local_sid", t_local != NULL);
    chk("local_sid has entries", t_local && t_local->nentries > 0);

    /* lookup against the loaded entries: fd00::2 should hit */
    if (t_local) {
        int dst_fid = rmt_field_id(p, "ipv6.dst");
        chk("field ipv6.dst exists", dst_fid >= 0);
        if (dst_fid >= 0) {
            phv_t phv; memset(&phv, 0, sizeof(phv));
            set_field_ipv6(&phv.fields[dst_fid], "fd00::2");
            rmt_entry_t *r = rmt_table_lookup(p, t_local, &phv);
            chk("lookup fd00::2 hits local_sid", r != NULL);

            /* lookup a non-matching address */
            set_field_ipv6(&phv.fields[dst_fid], "2001:db8::1");
            r = rmt_table_lookup(p, t_local, &phv);
            chk("lookup 2001:db8::1 misses local_sid", r == NULL);
        }
    }

    rmt_destroy(p);
}

/* ================================================================ */
/* Test 7: Edge cases                                               */
/* ================================================================ */
static void test_edge_cases(void)
{
    printf("\n---- Test 7: edge cases ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int fid = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "test");
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_LPM;
    t->default_action = -1;
    p->ntables = 1;

    /* lookup on empty table -> NULL */
    phv_t phv; memset(&phv, 0, sizeof(phv));
    set_field_ipv4(&phv.fields[fid], "10.0.0.1");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("empty table -> NULL", r == NULL);

    /* insert wildcard /0 (matches everything) */
    rmt_entry_t e; memset(&e, 0, sizeof(e));
    entry_set_key_ipv4(&e, 0, "0.0.0.0", 0);
    e.action_id = 7; e.params[0] = 777; e.nparams = 1;
    chk("insert 0.0.0.0/0 (wildcard)", rmt_table_insert(p, t, &e) != NULL);

    /* any address should match /0 */
    set_field_ipv4(&phv.fields[fid], "1.2.3.4");
    r = rmt_table_lookup(p, t, &phv);
    chk("1.2.3.4 matches /0 wildcard", r && r->params[0] == 777);

    set_field_ipv4(&phv.fields[fid], "255.255.255.255");
    r = rmt_table_lookup(p, t, &phv);
    chk("255.255.255.255 matches /0", r && r->params[0] == 777);

    /* insert more specific /32 -> should override /0 */
    entry_set_key_ipv4(&e, 0, "1.2.3.4", 32);
    e.action_id = 8; e.params[0] = 888; e.nparams = 1;
    rmt_table_insert(p, t, &e);

    set_field_ipv4(&phv.fields[fid], "1.2.3.4");
    r = rmt_table_lookup(p, t, &phv);
    chk("1.2.3.4 matches /32 over /0", r && r->params[0] == 888);

    /* other addresses still match /0 */
    set_field_ipv4(&phv.fields[fid], "1.2.3.5");
    r = rmt_table_lookup(p, t, &phv);
    chk("1.2.3.5 still matches /0", r && r->params[0] == 777);

    /* exact match table: try inserting with wrong prefix -> should fail */
    rmt_table_t *t2 = &p->tables[1];
    memset(t2, 0, sizeof(*t2));
    strcpy(t2->name, "exact_test");
    t2->nkeys = 1;
    t2->keys[0].field_id = fid;
    t2->keys[0].mtype = MATCH_EXACT;
    t2->default_action = -1;
    p->ntables = 2;

    rmt_entry_t e2; memset(&e2, 0, sizeof(e2));
    entry_set_key_ipv4(&e2, 0, "10.0.0.0", 24);  /* plen != 32, should fail */
    e2.action_id = 0; e2.nparams = 0;
    rmt_entry_t *bad = rmt_table_insert(p, t2, &e2);
    chk("exact table rejects plen != bitlen", bad == NULL);

    rmt_table_free(t);
    rmt_table_free(t2);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 8: Multi-field LPM cross-field backtracking (README §3.4)    */
/* Regression: the old greedy walk took the deepest field-0 prefix    */
/* and never backtracked, so a packet matching a shorter field-0      */
/* prefix whose continuation exists (while the longest field-0 prefix */
/* has none) wrongly MISSED.                                          */
/* ================================================================ */
static void test_multi_lpm_backtrack(void)
{
    printf("\n---- Test 8: multi-field LPM cross-field backtracking ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int f_src = rmt_field_add(p, hdr, "ipv4.src", 32);
    int f_dst = rmt_field_add(p, hdr, "ipv4.dst", 32);

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "policy");
    t->nkeys = 2;
    t->keys[0].field_id = f_src;
    t->keys[0].mtype = MATCH_LPM;
    t->keys[1].field_id = f_dst;
    t->keys[1].mtype = MATCH_LPM;
    t->default_action = -1;
    p->ntables = 1;

    rmt_entry_t e; memset(&e, 0, sizeof(e));
    e.action_id = 1; e.nparams = 1;

    /* entry A: src 10.0.0.0/8 + dst 0.0.0.0/0 (wildcard) -> param=100 */
    entry_set_key_ipv4(&e, 0, "10.0.0.0", 8);
    entry_set_key_ipv4(&e, 1, "0.0.0.0", 0);
    e.params[0] = 100;
    chk("insert A (10/8, any dst)", rmt_table_insert(p, t, &e) != NULL);

    /* entry B: src 10.1.0.0/16 + dst 192.168.0.0/16 -> param=200 */
    entry_set_key_ipv4(&e, 0, "10.1.0.0", 16);
    entry_set_key_ipv4(&e, 1, "192.168.0.0", 16);
    e.params[0] = 200;
    chk("insert B (10.1/16, 192.168/16)", rmt_table_insert(p, t, &e) != NULL);

    phv_t phv; memset(&phv, 0, sizeof(phv));

    /* (10.1.2.3, 192.168.1.1): field-0 longest prefix /16 (B) has a
     * dst completion -> B wins. */
    set_field_ipv4(&phv.fields[f_src], "10.1.2.3");
    set_field_ipv4(&phv.fields[f_dst], "192.168.1.1");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("(10.1.2.3, 192.168.1.1) -> B param=200", r && r->params[0] == 200);

    /* REGRESSION (report P0): (10.1.2.3, 8.8.8.8). Field-0 longest
     * prefix is still /16 (B), but B's dst 192.168/16 cannot match
     * 8.8.8.8; must backtrack to the shorter src prefix /8 (A), whose
     * dst wildcard completes. Old code returned NULL here. */
    set_field_ipv4(&phv.fields[f_dst], "8.8.8.8");
    r = rmt_table_lookup(p, t, &phv);
    chk("(10.1.2.3, 8.8.8.8) backtracks to A param=100",
        r && r->params[0] == 100);

    /* (10.9.9.9, 8.8.8.8): field-0 only matches /8 (A) directly. */
    set_field_ipv4(&phv.fields[f_src], "10.9.9.9");
    r = rmt_table_lookup(p, t, &phv);
    chk("(10.9.9.9, 8.8.8.8) -> A param=100", r && r->params[0] == 100);

    /* (11.0.0.1, 8.8.8.8): no field-0 candidate at all -> miss. */
    set_field_ipv4(&phv.fields[f_src], "11.0.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("(11.0.0.1, 8.8.8.8) -> miss", r == NULL);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* Deeper variant: 3 LPM keys, backtracking spans two field levels.   */
static void test_multi_lpm_backtrack_3key(void)
{
    printf("\n---- Test 8b: 3-key LPM backtracking across two levels ----\n");

    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv4", false);
    int f_a = rmt_field_add(p, hdr, "ipv4.src", 32);
    int f_b = rmt_field_add(p, hdr, "ipv4.dst", 32);
    int f_c = rmt_field_add(p, hdr, "ipv4.proto32", 32); /* synthetic */

    rmt_table_t *t = &p->tables[0];
    memset(t, 0, sizeof(*t));
    strcpy(t->name, "deep");
    t->nkeys = 3;
    t->keys[0].field_id = f_a; t->keys[0].mtype = MATCH_LPM;
    t->keys[1].field_id = f_b; t->keys[1].mtype = MATCH_LPM;
    t->keys[2].field_id = f_c; t->keys[2].mtype = MATCH_LPM;
    t->default_action = -1;
    p->ntables = 1;

    rmt_entry_t e; memset(&e, 0, sizeof(e));
    e.action_id = 1; e.nparams = 1;

    /* X: a/8  + b/8  + c 0/0 (wildcard) -> param=1 */
    entry_set_key_ipv4(&e, 0, "10.0.0.0", 8);
    entry_set_key_ipv4(&e, 1, "10.0.0.0", 8);
    entry_set_key_ipv4(&e, 2, "0.0.0.0", 0);
    e.params[0] = 1;
    chk("insert X (a/8, b/8, c any)", rmt_table_insert(p, t, &e) != NULL);

    /* Y: a/16 + b/16 + c 1.0.0.0/8 -> param=2 */
    entry_set_key_ipv4(&e, 0, "10.1.0.0", 16);
    entry_set_key_ipv4(&e, 1, "10.1.0.0", 16);
    entry_set_key_ipv4(&e, 2, "1.0.0.0", 8);
    e.params[0] = 2;
    chk("insert Y (a/16, b/16, c 1/8)", rmt_table_insert(p, t, &e) != NULL);

    phv_t phv; memset(&phv, 0, sizeof(phv));

    /* (10.1.2.3, 10.1.9.9, 1.2.3.4): Y fully matches -> 2 */
    set_field_ipv4(&phv.fields[f_a], "10.1.2.3");
    set_field_ipv4(&phv.fields[f_b], "10.1.9.9");
    set_field_ipv4(&phv.fields[f_c], "1.2.3.4");
    rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
    chk("full Y match -> param=2", r && r->params[0] == 2);

    /* (10.1.2.3, 10.1.9.9, 9.9.9.9): field-2 fails under Y (c 1/8);
     * backtrack: Y's a/16+b/16 has no other c, so climb all the way
     * to X (a/8+b/8, c wildcard) -> 1. Exercises two-level unwind. */
    set_field_ipv4(&phv.fields[f_c], "9.9.9.9");
    r = rmt_table_lookup(p, t, &phv);
    chk("c fails -> backtrack two levels to X param=1",
        r && r->params[0] == 1);

    /* (10.1.2.3, 10.9.9.9, 1.2.3.4): field-1 fails under Y's b/16;
     * backtrack to X's b/8 -> 1. */
    set_field_ipv4(&phv.fields[f_b], "10.9.9.9");
    set_field_ipv4(&phv.fields[f_c], "1.2.3.4");
    r = rmt_table_lookup(p, t, &phv);
    chk("b fails -> backtrack to X param=1", r && r->params[0] == 1);

    /* (10.1.2.3, 10.1.9.9, 1.2.3.4) but remove... keep simple: a
     * full miss outside every prefix. */
    set_field_ipv4(&phv.fields[f_a], "172.16.0.1");
    r = rmt_table_lookup(p, t, &phv);
    chk("outside all prefixes -> miss", r == NULL);

    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */
/* Test 9 / 9b: IPv6 LPM with a long shared prefix and mixed prefix  */
/* lengths. These stress node splitting (radix) and pin insertion-    */
/* order independence of the lookup result.                           */
/* ================================================================ */
struct ipv6spec { const char *ip; int plen; uint64_t param; };
static const struct ipv6spec ipv6set[] = {
    { "fd00::",          16,  1 },
    { "fd00:0:0:1::",    64,  2 },
    { "fd00:0:0:1::1",  128,  3 },
    { "fd00:0:0:1::2",  128,  4 },
    { "fd00:0:0:2::1",  128,  5 },
};

static rmt_entry_t *e_init(rmt_entry_t *e, int idx)
{
    entry_set_key_ipv6(e, 0, ipv6set[idx].ip, ipv6set[idx].plen);
    e->params[0] = ipv6set[idx].param;
    return e;
}

static rmt_table_t *ipv6_lpm_build(rmt_pipeline_t *p, int fid,
                                   const int *order, int n, const char *name)
{
    rmt_table_t *t = &p->tables[p->ntables];
    p->ntables++;
    memset(t, 0, sizeof(*t));
    strcpy(t->name, name);
    t->nkeys = 1;
    t->keys[0].field_id = fid;
    t->keys[0].mtype = MATCH_LPM;
    t->default_action = -1;
    rmt_entry_t e; memset(&e, 0, sizeof(e));
    e.action_id = 1; e.nparams = 1;
    for (int i = 0; i < n; i++) {
        int idx = order[i];
        chk("  insert", rmt_table_insert(p, t, e_init(&e, idx)) != NULL);
    }
    return t;
}

static void ipv6_lpm_check_lookups(rmt_pipeline_t *p, rmt_table_t *t, int fid)
{
    phv_t phv; memset(&phv, 0, sizeof(phv));
    struct { const char *ip; uint64_t expect; int hit; } cases[] = {
        { "fd00:0:0:1::1",  3, 1 },   /* /128 exact leaf   */
        { "fd00:0:0:1::2",  4, 1 },   /* /128 exact leaf   */
        { "fd00:0:0:1::99", 2, 1 },   /* falls to /64      */
        { "fd00:0:0:2::1",  5, 1 },   /* /128 exact leaf   */
        { "fd00:0:0:3::1",  1, 1 },   /* falls to /16      */
        { "fd00:1::1",      1, 1 },   /* falls to /16      */
        { "fe80::1",        0, 0 },   /* no match          */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        set_field_ipv6(&phv.fields[fid], cases[i].ip);
        rmt_entry_t *r = rmt_table_lookup(p, t, &phv);
        char msg[96];
        if (cases[i].hit) {
            snprintf(msg, sizeof(msg), "%s -> param=%llu", cases[i].ip,
                     (unsigned long long)cases[i].expect);
            chk(msg, r && r->params[0] == cases[i].expect);
        } else {
            snprintf(msg, sizeof(msg), "%s -> miss", cases[i].ip);
            chk(msg, r == NULL);
        }
    }
}

static void test_ipv6_lpm_split(void)
{
    printf("\n---- Test 9: IPv6 LPM shared prefix, longest-first inserts ----\n");
    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv6", false);
    int fid = rmt_field_add(p, hdr, "ipv6.dst", 128);
    int order_desc[] = { 2, 3, 4, 1, 0 };   /* /128 leaves before /16 */
    rmt_table_t *t = ipv6_lpm_build(p, fid, order_desc, 5, "v6_desc");
    ipv6_lpm_check_lookups(p, t, fid);
    rmt_table_free(t);
    rmt_destroy(p);
}

static void test_ipv6_lpm_order_indep(void)
{
    printf("\n---- Test 9b: IPv6 LPM insertion-order independence ----\n");
    rmt_pipeline_t *p = rmt_create();
    int hdr = rmt_hdr_add(p, "ipv6", false);
    int fid = rmt_field_add(p, hdr, "ipv6.dst", 128);
    int order_asc[] = { 0, 1, 2, 3, 4 };    /* /16 first, leaves last */
    rmt_table_t *t = ipv6_lpm_build(p, fid, order_asc, 5, "v6_asc");
    ipv6_lpm_check_lookups(p, t, fid);
    rmt_table_free(t);
    rmt_destroy(p);
}

/* ================================================================ */

int main(void)
{
    printf("======== Match Table Runtime Interface Tests ========\n");

    test_exact_single();
    test_lpm_single();
    test_multi_key();
    test_default_action();
    test_first_insert_wins();
    test_load_entries();
    test_edge_cases();
    test_multi_lpm_backtrack();
    test_multi_lpm_backtrack_3key();
    test_ipv6_lpm_split();
    test_ipv6_lpm_order_indep();

    printf("\n====================================================\n");
    printf("%s (%d failure%s)\n",
           g_fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           g_fails, g_fails == 1 ? "" : "s");
    return g_fails ? 1 : 0;
}
