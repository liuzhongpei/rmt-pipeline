/*
 * runtime.c - control plane: install table entries from a command file
 *
 * Syntax (one command per line, '#' starts a comment):
 *
 *   table_add <table> <action> <key1> [<key2> ...] => [param1 ...]
 *   table_set_default <table> <action> [param1 ...]
 *
 * Key values:  decimal / 0x hex, IPv4 dotted, IPv6, aa:bb:cc:dd:ee:ff MAC;
 *              an optional "/len" suffix gives the LPM prefix length.
 * Params are 64-bit numbers (decimal / 0x hex / MAC).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include "rmt.h"

static int parse_u64(const char *s, uint64_t *out)
{
    /* dotted IPv4 as a 32-bit value */
    if (strchr(s, '.')) {
        struct in_addr a4;
        if (inet_pton(AF_INET, s, &a4) == 1) {
            *out = ntohl(a4.s_addr);
            return 0;
        }
    }
    /* MAC style aa:bb:cc:dd:ee:ff */
    if (strchr(s, ':') && strlen(s) == 17) {
        unsigned b[6];
        if (sscanf(s, "%x:%x:%x:%x:%x:%x",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            uint64_t v = 0;
            for (int i = 0; i < 6; i++)
                v = (v << 8) | (b[i] & 0xff);
            *out = v;
            return 0;
        }
    }
    char *end;
    errno = 0;
    *out = strtoull(s, &end, 0);
    if (errno == ERANGE || *end != 0) return -1;
    return 0;
}

/* parse a key token into a right-aligned 128-bit value + prefix len */
static int parse_key(const char *tok, int bitlen, uint8_t out[16],
                     uint16_t *plen)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "%s", tok);
    memset(out, 0, 16);
    *plen = (uint16_t)bitlen;   /* default: exact */

    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = 0;
        int p = atoi(slash + 1);
        if (p < 0 || p > bitlen) return -1;   /* prefix len out of range */
        *plen = (uint16_t)p;
    }

    if (strchr(buf, ':')) {
        struct in6_addr a6;
        if (inet_pton(AF_INET6, buf, &a6) == 1) {
            memcpy(out, a6.s6_addr, 16);        /* 128-bit, aligned    */
            return 0;
        }
        uint64_t v;                              /* MAC as 48-bit value */
        if (parse_u64(buf, &v) == 0) {
            for (int i = 0; i < 8; i++)
                out[15 - i] = (uint8_t)(v >> (8 * i));
            return 0;
        }
        return -1;
    }
    if (strchr(buf, '.')) {
        struct in_addr a4;
        if (inet_pton(AF_INET, buf, &a4) == 1) {
            uint32_t v = ntohl(a4.s_addr);
            out[12] = (uint8_t)(v >> 24);
            out[13] = (uint8_t)(v >> 16);
            out[14] = (uint8_t)(v >> 8);
            out[15] = (uint8_t)v;
            return 0;
        }
        return -1;
    }
    /* long hex literal (> 64 bits, e.g. a 128-bit NDN name) */
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X') &&
        strlen(buf) > 18) {
        const char *h = buf + 2;
        size_t nd = strlen(h);
        if (nd > 32)
            return -1;
        for (size_t i = 0; i < nd; i++) {          /* right aligned */
            char ch = h[nd - 1 - i];
            int v;
            if (ch >= '0' && ch <= '9') v = ch - '0';
            else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
            else return -1;
            out[15 - i / 2] |= (uint8_t)(v << ((i & 1) * 4));
        }
        return 0;
    }
    uint64_t v;
    if (parse_u64(buf, &v) != 0)
        return -1;
    for (int i = 0; i < 8; i++)
        out[15 - i] = (uint8_t)(v >> (8 * i));
    return 0;
}

static rmt_table_t *find_table(rmt_pipeline_t *p, const char *n)
{
    for (int i = 0; i < p->ntables; i++)
        if (!strcmp(p->tables[i].name, n))
            return &p->tables[i];
    return NULL;
}

static int find_action(rmt_pipeline_t *p, const char *n)
{
    for (int i = 0; i < p->nactions; i++)
        if (!strcmp(p->actions[i].name, n))
            return i;
    return -1;
}

int rmt_load_entries(rmt_pipeline_t *p, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "runtime: cannot open %s\n", path);
        return -1;
    }

    char line[512];
    int lineno = 0, added = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;

        char *tok[32];
        int nt = 0;
        for (char *s = strtok(line, " \t\r\n");
             s && nt < 32; s = strtok(NULL, " \t\r\n"))
            tok[nt++] = s;
        if (nt == 0)
            continue;

        if (!strcmp(tok[0], "table_add")) {
            if (nt < 3)
                goto bad;
            rmt_table_t *t = find_table(p, tok[1]);
            int aid = find_action(p, tok[2]);
            if (!t || aid < 0)
                goto bad;

            rmt_entry_t e;
            memset(&e, 0, sizeof(e));
            e.action_id = aid;

            int i = 3;
            int k = 0;
            for (; i < nt && strcmp(tok[i], "=>"); i++, k++) {
                if (k >= t->nkeys)
                    goto bad;
                if (parse_key(tok[i], p->fdefs[t->keys[k].field_id].bitlen,
                              e.key[k], &e.plen[k]) < 0)
                    goto bad;
            }
            if (k != t->nkeys)
                goto bad;
            if (i < nt && !strcmp(tok[i], "=>"))
                i++;
            for (; i < nt; i++) {
                if (e.nparams >= RMT_MAX_PARAMS)
                    goto bad;
                if (parse_u64(tok[i], &e.params[e.nparams++]) < 0)
                    goto bad;
            }
            if (!rmt_table_insert(p, t, &e))
                goto bad;
            added++;
        } else if (!strcmp(tok[0], "table_set_default")) {
            if (nt < 3)
                goto bad;
            rmt_table_t *t = find_table(p, tok[1]);
            int aid = find_action(p, tok[2]);
            if (!t || aid < 0)
                goto bad;
            t->default_action = aid;
            for (int i = 3, np = 0; i < nt && np < RMT_MAX_PARAMS; i++, np++)
                if (parse_u64(tok[i], &t->def_params[np]) < 0)
                    goto bad;
        } else {
            goto bad;
        }
        continue;
bad:
        fprintf(stderr, "runtime: %s:%d: bad command\n", path, lineno);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    printf("runtime: installed %d entries from %s\n", added, path);
    return 0;
}
