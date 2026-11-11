/*
 * test_alu.c - standalone unit tests for PHV field operations
 *
 * Tests the PHV API (field value get/set, validity bits, masking)
 * independently of the full pipeline. The ALU arithmetic operations
 * (add/sub/and/or/xor/shl/shr) are exercised by the full pipeline
 * tests in tests.c and test_match.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rmt.h"

static int pass = 0, fail = 0;

#define CHK(msg, cond) do { \
    if (cond) { printf("  [PASS] %s\n", msg); pass++; } \
    else      { printf("  [FAIL] %s\n", msg); fail++; } \
} while (0)

/* ---- Test 1: basic field value operations via PHV inline API ---- */
static void test_field_value_ops(void)
{
    printf("\n---- ALU Test 1: field value operations ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "test", false);
    assert(h >= 0);
    int f1 = rmt_field_add(p, h, "test.f1", 32);
    int f2 = rmt_field_add(p, h, "test.f2", 16);
    int f3 = rmt_field_add(p, h, "test.f3", 8);
    assert(f1 >= 0 && f2 >= 0 && f3 >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    /* fv_set_u64 / fv_get_u64 round-trip */
    fv_set_u64(&phv.fields[f1], 0xDEADBEEF);
    CHK("32-bit set/get 0xDEADBEEF",
        fv_get_u64(&phv.fields[f1]) == 0xDEADBEEF);

    fv_set_u64(&phv.fields[f2], 0x1234);
    CHK("16-bit set/get 0x1234",
        fv_get_u64(&phv.fields[f2]) == 0x1234);

    fv_set_u64(&phv.fields[f3], 0xAB);
    CHK("8-bit set/get 0xAB",
        fv_get_u64(&phv.fields[f3]) == 0xAB);

    /* Truncation: 32-bit value into 16-bit field keeps low bits */
    fv_set_u64(&phv.fields[f2], 0xCAFEBABE);
    CHK("16-bit truncation keeps low 16 bits",
        fv_get_u64(&phv.fields[f2]) == 0xBABE);

    /* Zero-width value */
    fv_set_u64(&phv.fields[f3], 0);
    CHK("8-bit zero", fv_get_u64(&phv.fields[f3]) == 0);

    rmt_destroy(p);
}

/* ---- Test 2: validity bit operations ---- */
static void test_validity_bits(void)
{
    printf("\n---- ALU Test 2: header validity bits ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h1 = rmt_hdr_add(p, "eth", false);
    int h2 = rmt_hdr_add(p, "ipv4", false);
    int h3 = rmt_hdr_add(p, "udp", false);
    int f1 = rmt_field_add(p, h1, "eth.dst", 48);
    int f2 = rmt_field_add(p, h2, "ipv4.dst", 32);
    int f3 = rmt_field_add(p, h3, "udp.dport", 16);
    assert(h1 >= 0 && h2 >= 0 && h3 >= 0);
    assert(f1 >= 0 && f2 >= 0 && f3 >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    CHK("initially no headers valid",
        !phv_hdr_valid(&phv, h1) && !phv_hdr_valid(&phv, h2) &&
        !phv_hdr_valid(&phv, h3));

    phv_hdr_set(&phv, h1);
    CHK("eth marked valid", phv_hdr_valid(&phv, h1));
    CHK("ipv4 still invalid", !phv_hdr_valid(&phv, h2));

    phv_hdr_set(&phv, h2);
    phv_hdr_set(&phv, h3);
    CHK("all three headers valid",
        phv_hdr_valid(&phv, h1) && phv_hdr_valid(&phv, h2) &&
        phv_hdr_valid(&phv, h3));

    phv_hdr_clr(&phv, h1);
    CHK("eth cleared, others still valid",
        !phv_hdr_valid(&phv, h1) && phv_hdr_valid(&phv, h2) &&
        phv_hdr_valid(&phv, h3));

    phv_hdr_clr(&phv, h2);
    phv_hdr_clr(&phv, h3);
    CHK("all cleared",
        !phv_hdr_valid(&phv, h1) && !phv_hdr_valid(&phv, h2) &&
        !phv_hdr_valid(&phv, h3));

    rmt_destroy(p);
}

/* ---- Test 3: field masking ---- */
static void test_field_masking(void)
{
    printf("\n---- ALU Test 3: field masking ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "t", false);
    /* 12-bit field: mask should keep low 12 bits */
    int f12 = rmt_field_add(p, h, "t.x12", 12);
    /* 20-bit field */
    int f20 = rmt_field_add(p, h, "t.x20", 20);
    /* 1-bit field */
    int f1 = rmt_field_add(p, h, "t.x1", 1);
    assert(f12 >= 0 && f20 >= 0 && f1 >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    /* 12-bit: 0xFFF should survive, 0x1234 should be truncated */
    fv_set_u64(&phv.fields[f12], 0x1234);
    CHK("12-bit truncation 0x1234 -> 0x234",
        fv_get_u64(&phv.fields[f12]) == 0x234);

    fv_set_u64(&phv.fields[f12], 0xFFF);
    CHK("12-bit max 0xFFF survives",
        fv_get_u64(&phv.fields[f12]) == 0xFFF);

    /* 20-bit: 0xFFFFF should survive */
    fv_set_u64(&phv.fields[f20], 0xABCDE1234ULL);
    CHK("20-bit truncation keeps low 20 bits",
        fv_get_u64(&phv.fields[f20]) == 0xDE1234 % (1U << 20));

    fv_set_u64(&phv.fields[f20], 0xFFFFF);
    CHK("20-bit max 0xFFFFF survives",
        fv_get_u64(&phv.fields[f20]) == 0xFFFFF);

    /* 1-bit: only 0 or 1 */
    fv_set_u64(&phv.fields[f1], 0);
    CHK("1-bit 0", fv_get_u64(&phv.fields[f1]) == 0);
    fv_set_u64(&phv.fields[f1], 1);
    CHK("1-bit 1", fv_get_u64(&phv.fields[f1]) == 1);
    fv_set_u64(&phv.fields[f1], 7);
    CHK("1-bit 7 -> 1 (low bit)", fv_get_u64(&phv.fields[f1]) == 1);

    rmt_destroy(p);
}

/* ---- Test 4: 128-bit (wide) field operations ---- */
static void test_wide_fields(void)
{
    printf("\n---- ALU Test 4: 128-bit IPv6 field operations ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "ipv6", false);
    int f = rmt_field_add(p, h, "ipv6.dst", 128);
    assert(f >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    /* 128-bit all-ones */
    memset(phv.fields[f].v, 0xFF, 16);
    fv_mask(&phv.fields[f]);
    /* For 128-bit field, mask should be all-ones (no truncation) */
    int all_ones = 1;
    for (int i = 0; i < 16; i++)
        if (phv.fields[f].v[i] != 0xFF) all_ones = 0;
    CHK("128-bit all-ones survives masking", all_ones);

    /* 128-bit clear */
    fv_clear(&phv.fields[f]);
    int all_zero = 1;
    for (int i = 0; i < 16; i++)
        if (phv.fields[f].v[i] != 0) all_zero = 0;
    CHK("128-bit clear zeros all bytes", all_zero);

    /* 128-bit set_u64 writes to low 8 bytes */
    fv_set_u64(&phv.fields[f], 0x0102030405060708ULL);
    CHK("128-bit set_u64 low bytes",
        fv_get_u64(&phv.fields[f]) == 0x0102030405060708ULL);

    rmt_destroy(p);
}

/* ---- Test 5: bit-level operations ---- */
static void test_bit_ops(void)
{
    printf("\n---- ALU Test 5: bit-level operations ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "t", false);
    int f = rmt_field_add(p, h, "t.x", 16);
    assert(f >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    fv_clear(&phv.fields[f]);

    /* Big-endian bit layout: bit 0 = MSB (0x8000), bit 15 = LSB (0x0001) */
    fv_set_bit(&phv.fields[f], 0, 1);
    CHK("bit 0 set (MSB)", fv_get_bit(&phv.fields[f], 0) == 1);
    CHK("value after bit 0 = 0x8000", fv_get_u64(&phv.fields[f]) == 0x8000);

    fv_set_bit(&phv.fields[f], 15, 1);
    CHK("bit 15 set (LSB)", fv_get_bit(&phv.fields[f], 15) == 1);
    CHK("value = 0x8001", fv_get_u64(&phv.fields[f]) == 0x8001);

    /* Clear MSB */
    fv_set_bit(&phv.fields[f], 0, 0);
    CHK("bit 0 cleared", fv_get_bit(&phv.fields[f], 0) == 0);
    CHK("value = 0x0001", fv_get_u64(&phv.fields[f]) == 0x0001);

    /* Test a middle bit (bit 8 = 0x0080) */
    fv_set_bit(&phv.fields[f], 8, 1);
    CHK("bit 8 set", fv_get_bit(&phv.fields[f], 8) == 1);
    CHK("value = 0x0081", fv_get_u64(&phv.fields[f]) == 0x0081);

    rmt_destroy(p);
}

/* ---- Test 6: fv_copy ---- */
static void test_fv_copy(void)
{
    printf("\n---- ALU Test 6: fv_copy ----\n");

    rmt_pipeline_t *p = rmt_create();
    assert(p);
    int h = rmt_hdr_add(p, "t", false);
    int f1 = rmt_field_add(p, h, "t.a", 24);
    int f2 = rmt_field_add(p, h, "t.b", 24);
    assert(f1 >= 0 && f2 >= 0);
    rmt_finalize(p);

    phv_t phv;
    memset(&phv, 0, sizeof(phv));
    rmt_phv_reset(p, &phv);

    fv_set_u64(&phv.fields[f1], 0xABCDEF);
    fv_copy(&phv.fields[f2], &phv.fields[f1]);
    CHK("24-bit copy preserves value",
        fv_get_u64(&phv.fields[f2]) == 0xABCDEF);

    /* Modify source, dest should be unchanged */
    fv_set_u64(&phv.fields[f1], 0x111111);
    CHK("copy is independent (dest unchanged)",
        fv_get_u64(&phv.fields[f2]) == 0xABCDEF);

    rmt_destroy(p);
}

int main(void)
{
    printf("====================================================\n");
    printf("PHV / ALU Unit Tests (standalone)\n");
    printf("====================================================\n");

    test_field_value_ops();
    test_validity_bits();
    test_field_masking();
    test_wide_fields();
    test_bit_ops();
    test_fv_copy();

    printf("\n====================================================\n");
    if (fail == 0)
        printf("ALL TESTS PASSED (%d checks)\n", pass);
    else
        printf("TESTS: %d passed, %d failed\n", pass, fail);
    printf("====================================================\n");
    return fail ? 1 : 0;
}
