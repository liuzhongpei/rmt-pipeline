/*
 * phv.c - PHV field value helpers (128-bit big-endian, right aligned)
 *
 * The hot-path accessors (fv_get_bit / fv_set_bit / fv_get_u64 / ...)
 * live as static inlines in rmt.h so they inline into the parser,
 * match and ALU engines; only the cold formatting helper stays here.
 */
#include <stdio.h>
#include <string.h>
#include "rmt.h"

const char *fv_str(const fv_t *f, char *buf, size_t n)
{
    int nbytes = (f->bitlen + 7) / 8;
    if (nbytes == 0) nbytes = 1;
    size_t off = 0;
    off += (size_t)snprintf(buf + off, n - off, "0x");
    for (int i = 16 - nbytes; i < 16 && off + 3 < n; i++)
        off += (size_t)snprintf(buf + off, n - off, "%02x", f->v[i]);
    return buf;
}
