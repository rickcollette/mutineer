/*
 * PLANK CBOR Tests
 * Tests for canonical CBOR encoding/decoding
 */

#include "plank/plank_cbor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

/* ============================================================================
 * ENCODING TESTS
 * ============================================================================ */

static int test_encode_uint_small(void) {
    uint8_t buf[16];
    cbor_encoder_t enc;
    cbor_encoder_init(&enc, buf, sizeof(buf));
    
    cbor_encode_uint(&enc, 0);
    if (enc.len != 1 || buf[0] != 0x00) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_uint(&enc, 23);
    if (enc.len != 1 || buf[0] != 0x17) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_uint(&enc, 24);
    if (enc.len != 2 || buf[0] != 0x18 || buf[1] != 24) return 0;
    
    return 1;
}

static int test_encode_uint_large(void) {
    uint8_t buf[16];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_uint(&enc, 256);
    if (enc.len != 3 || buf[0] != 0x19) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_uint(&enc, 65536);
    if (enc.len != 5 || buf[0] != 0x1a) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_uint(&enc, 0x100000000ULL);
    if (enc.len != 9 || buf[0] != 0x1b) return 0;
    
    return 1;
}

static int test_encode_int_negative(void) {
    uint8_t buf[16];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_int(&enc, -1);
    if (enc.len != 1 || buf[0] != 0x20) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_int(&enc, -24);
    if (enc.len != 1 || buf[0] != 0x37) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_int(&enc, -25);
    if (enc.len != 2 || buf[0] != 0x38 || buf[1] != 24) return 0;
    
    return 1;
}

static int test_encode_bytes(void) {
    uint8_t buf[32];
    cbor_encoder_t enc;
    
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_bytes(&enc, data, 4);
    
    if (enc.len != 5) return 0;
    if (buf[0] != 0x44) return 0;  /* 0x40 | 4 */
    if (memcmp(buf + 1, data, 4) != 0) return 0;
    
    return 1;
}

static int test_encode_text(void) {
    uint8_t buf[32];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_text(&enc, "hello");
    
    if (enc.len != 6) return 0;
    if (buf[0] != 0x65) return 0;  /* 0x60 | 5 */
    if (memcmp(buf + 1, "hello", 5) != 0) return 0;
    
    return 1;
}

static int test_encode_array(void) {
    uint8_t buf[32];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_array(&enc, 3);
    cbor_encode_uint(&enc, 1);
    cbor_encode_uint(&enc, 2);
    cbor_encode_uint(&enc, 3);
    
    if (enc.len != 4) return 0;
    if (buf[0] != 0x83) return 0;  /* 0x80 | 3 */
    if (buf[1] != 0x01 || buf[2] != 0x02 || buf[3] != 0x03) return 0;
    
    return 1;
}

static int test_encode_map(void) {
    uint8_t buf[32];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_map(&enc, 2);
    cbor_encode_text(&enc, "a");
    cbor_encode_uint(&enc, 1);
    cbor_encode_text(&enc, "b");
    cbor_encode_uint(&enc, 2);
    
    if (enc.len != 7) return 0;
    if (buf[0] != 0xa2) return 0;  /* 0xa0 | 2 */
    
    return 1;
}

static int test_encode_bool(void) {
    uint8_t buf[8];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_bool(&enc, true);
    if (enc.len != 1 || buf[0] != 0xf5) return 0;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_bool(&enc, false);
    if (enc.len != 1 || buf[0] != 0xf4) return 0;
    
    return 1;
}

static int test_encode_null(void) {
    uint8_t buf[8];
    cbor_encoder_t enc;
    
    cbor_encoder_init(&enc, buf, sizeof(buf));
    cbor_encode_null(&enc);
    
    if (enc.len != 1 || buf[0] != 0xf6) return 0;
    
    return 1;
}

/* ============================================================================
 * DECODING TESTS
 * ============================================================================ */

static int test_decode_uint(void) {
    uint8_t data1[] = {0x00};
    uint8_t data2[] = {0x17};
    uint8_t data3[] = {0x18, 0x18};
    uint8_t data4[] = {0x19, 0x01, 0x00};
    
    cbor_decoder_t dec;
    
    cbor_decoder_init(&dec, data1, sizeof(data1));
    if (cbor_decode_uint(&dec) != 0) return 0;
    
    cbor_decoder_init(&dec, data2, sizeof(data2));
    if (cbor_decode_uint(&dec) != 23) return 0;
    
    cbor_decoder_init(&dec, data3, sizeof(data3));
    if (cbor_decode_uint(&dec) != 24) return 0;
    
    cbor_decoder_init(&dec, data4, sizeof(data4));
    if (cbor_decode_uint(&dec) != 256) return 0;
    
    return 1;
}

static int test_decode_int(void) {
    uint8_t data1[] = {0x20};
    uint8_t data2[] = {0x37};
    uint8_t data3[] = {0x38, 0x18};
    
    cbor_decoder_t dec;
    
    cbor_decoder_init(&dec, data1, sizeof(data1));
    if (cbor_decode_int(&dec) != -1) return 0;
    
    cbor_decoder_init(&dec, data2, sizeof(data2));
    if (cbor_decode_int(&dec) != -24) return 0;
    
    cbor_decoder_init(&dec, data3, sizeof(data3));
    if (cbor_decode_int(&dec) != -25) return 0;
    
    return 1;
}

static int test_decode_bytes(void) {
    uint8_t data[] = {0x44, 0x01, 0x02, 0x03, 0x04};
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, data, sizeof(data));
    
    size_t len;
    const uint8_t* bytes = cbor_decode_bytes(&dec, &len);
    
    if (!bytes || len != 4) return 0;
    if (bytes[0] != 0x01 || bytes[3] != 0x04) return 0;
    
    return 1;
}

static int test_decode_text(void) {
    uint8_t data[] = {0x65, 'h', 'e', 'l', 'l', 'o'};
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, data, sizeof(data));
    
    size_t len;
    const char* text = cbor_decode_text(&dec, &len);
    
    if (!text || len != 5) return 0;
    if (memcmp(text, "hello", 5) != 0) return 0;
    
    return 1;
}

static int test_decode_array(void) {
    uint8_t data[] = {0x83, 0x01, 0x02, 0x03};
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, data, sizeof(data));
    
    size_t count = cbor_decode_array(&dec);
    if (count != 3) return 0;
    
    if (cbor_decode_uint(&dec) != 1) return 0;
    if (cbor_decode_uint(&dec) != 2) return 0;
    if (cbor_decode_uint(&dec) != 3) return 0;
    
    return 1;
}

static int test_decode_map(void) {
    uint8_t data[] = {0xa2, 0x61, 'a', 0x01, 0x61, 'b', 0x02};
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, data, sizeof(data));
    
    size_t count = cbor_decode_map(&dec);
    if (count != 2) return 0;
    
    size_t key_len;
    const char* key = cbor_decode_text(&dec, &key_len);
    if (!key || key_len != 1 || key[0] != 'a') return 0;
    if (cbor_decode_uint(&dec) != 1) return 0;
    
    key = cbor_decode_text(&dec, &key_len);
    if (!key || key_len != 1 || key[0] != 'b') return 0;
    if (cbor_decode_uint(&dec) != 2) return 0;
    
    return 1;
}

static int test_decode_bool(void) {
    uint8_t data_true[] = {0xf5};
    uint8_t data_false[] = {0xf4};
    
    cbor_decoder_t dec;
    
    cbor_decoder_init(&dec, data_true, sizeof(data_true));
    if (!cbor_decode_bool(&dec)) return 0;
    
    cbor_decoder_init(&dec, data_false, sizeof(data_false));
    if (cbor_decode_bool(&dec)) return 0;
    
    return 1;
}

/* ============================================================================
 * MAP BUILDER TESTS (CANONICAL ORDERING)
 * ============================================================================ */

static int test_map_builder_sorting(void) {
    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);
    
    /* Add entries in non-canonical order */
    uint8_t buf[16];
    cbor_encoder_t tmp;
    
    /* "bb" -> 2 */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 2);
    cbor_map_builder_add(&mb, "bb", buf, tmp.len);
    
    /* "a" -> 1 */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 1);
    cbor_map_builder_add(&mb, "a", buf, tmp.len);
    
    /* "ccc" -> 3 */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 3);
    cbor_map_builder_add(&mb, "ccc", buf, tmp.len);
    
    /* Encode */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 64);
    cbor_map_builder_encode(&mb, &enc);
    
    /* Verify canonical order: "a" < "bb" < "ccc" (by length, then lexicographic) */
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, enc.buf, enc.len);
    
    size_t count = cbor_decode_map(&dec);
    if (count != 3) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    
    size_t key_len;
    const char* key;
    
    /* First: "a" */
    key = cbor_decode_text(&dec, &key_len);
    if (key_len != 1 || key[0] != 'a') {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    cbor_decode_uint(&dec);
    
    /* Second: "bb" */
    key = cbor_decode_text(&dec, &key_len);
    if (key_len != 2 || memcmp(key, "bb", 2) != 0) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    cbor_decode_uint(&dec);
    
    /* Third: "ccc" */
    key = cbor_decode_text(&dec, &key_len);
    if (key_len != 3 || memcmp(key, "ccc", 3) != 0) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    
    cbor_encoder_free(&enc);
    cbor_map_builder_free(&mb);
    return 1;
}

static int test_map_builder_same_length(void) {
    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);
    
    uint8_t buf[16];
    cbor_encoder_t tmp;
    
    /* Add entries with same length in non-lexicographic order */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 3);
    cbor_map_builder_add(&mb, "cc", buf, tmp.len);
    
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 1);
    cbor_map_builder_add(&mb, "aa", buf, tmp.len);
    
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, 2);
    cbor_map_builder_add(&mb, "bb", buf, tmp.len);
    
    /* Encode */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 64);
    cbor_map_builder_encode(&mb, &enc);
    
    /* Verify lexicographic order: "aa" < "bb" < "cc" */
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, enc.buf, enc.len);
    
    cbor_decode_map(&dec);
    
    size_t key_len;
    const char* key;
    
    key = cbor_decode_text(&dec, &key_len);
    if (memcmp(key, "aa", 2) != 0) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    cbor_decode_uint(&dec);
    
    key = cbor_decode_text(&dec, &key_len);
    if (memcmp(key, "bb", 2) != 0) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    cbor_decode_uint(&dec);
    
    key = cbor_decode_text(&dec, &key_len);
    if (memcmp(key, "cc", 2) != 0) {
        cbor_encoder_free(&enc);
        cbor_map_builder_free(&mb);
        return 0;
    }
    
    cbor_encoder_free(&enc);
    cbor_map_builder_free(&mb);
    return 1;
}

/* ============================================================================
 * ROUND-TRIP TESTS
 * ============================================================================ */

static int test_roundtrip_complex(void) {
    /* Encode a complex structure */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 256);
    
    cbor_encode_map(&enc, 3);
    
    cbor_encode_text(&enc, "name");
    cbor_encode_text(&enc, "test");
    
    cbor_encode_text(&enc, "values");
    cbor_encode_array(&enc, 3);
    cbor_encode_uint(&enc, 100);
    cbor_encode_uint(&enc, 200);
    cbor_encode_uint(&enc, 300);
    
    cbor_encode_text(&enc, "active");
    cbor_encode_bool(&enc, true);
    
    if (!cbor_encoder_ok(&enc)) {
        cbor_encoder_free(&enc);
        return 0;
    }
    
    /* Decode and verify */
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, enc.buf, enc.len);
    
    size_t map_count = cbor_decode_map(&dec);
    if (map_count != 3) {
        cbor_encoder_free(&enc);
        return 0;
    }
    
    /* name */
    size_t key_len;
    cbor_decode_text(&dec, &key_len);
    size_t val_len;
    const char* val = cbor_decode_text(&dec, &val_len);
    if (val_len != 4 || memcmp(val, "test", 4) != 0) {
        cbor_encoder_free(&enc);
        return 0;
    }
    
    /* values */
    cbor_decode_text(&dec, &key_len);
    size_t arr_count = cbor_decode_array(&dec);
    if (arr_count != 3) {
        cbor_encoder_free(&enc);
        return 0;
    }
    if (cbor_decode_uint(&dec) != 100) {
        cbor_encoder_free(&enc);
        return 0;
    }
    if (cbor_decode_uint(&dec) != 200) {
        cbor_encoder_free(&enc);
        return 0;
    }
    if (cbor_decode_uint(&dec) != 300) {
        cbor_encoder_free(&enc);
        return 0;
    }
    
    /* active */
    cbor_decode_text(&dec, &key_len);
    if (!cbor_decode_bool(&dec)) {
        cbor_encoder_free(&enc);
        return 0;
    }
    
    cbor_encoder_free(&enc);
    return 1;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("PLANK CBOR Tests\n");
    printf("================\n\n");
    
    printf("Encoding tests:\n");
    TEST(encode_uint_small);
    TEST(encode_uint_large);
    TEST(encode_int_negative);
    TEST(encode_bytes);
    TEST(encode_text);
    TEST(encode_array);
    TEST(encode_map);
    TEST(encode_bool);
    TEST(encode_null);
    
    printf("\nDecoding tests:\n");
    TEST(decode_uint);
    TEST(decode_int);
    TEST(decode_bytes);
    TEST(decode_text);
    TEST(decode_array);
    TEST(decode_map);
    TEST(decode_bool);
    
    printf("\nMap builder tests:\n");
    TEST(map_builder_sorting);
    TEST(map_builder_same_length);
    
    printf("\nRound-trip tests:\n");
    TEST(roundtrip_complex);
    
    printf("\n================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
