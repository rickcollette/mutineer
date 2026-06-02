/*
 * PLANK Crypto Tests
 * Tests for cryptographic primitives
 */

#include "plank/plank_crypto.h"
#include "plank/plank_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * SHA-256 TESTS
 * ============================================================================ */

static int test_sha256_empty(void) {
    uint8_t hash[32];
    
    if (!plank_crypto_sha256((const uint8_t*)"", 0, hash)) {
        return 0;
    }
    
    /* Expected: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    uint8_t expected[] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    
    return memcmp(hash, expected, 32) == 0;
}

static int test_sha256_hello(void) {
    uint8_t hash[32];
    
    if (!plank_crypto_sha256((const uint8_t*)"hello", 5, hash)) {
        return 0;
    }
    
    /* Expected: 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 */
    uint8_t expected[] = {
        0x2c, 0xf2, 0x4d, 0xba, 0x5f, 0xb0, 0xa3, 0x0e,
        0x26, 0xe8, 0x3b, 0x2a, 0xc5, 0xb9, 0xe2, 0x9e,
        0x1b, 0x16, 0x1e, 0x5c, 0x1f, 0xa7, 0x42, 0x5e,
        0x73, 0x04, 0x33, 0x62, 0x93, 0x8b, 0x98, 0x24
    };
    
    return memcmp(hash, expected, 32) == 0;
}

static int test_sha256_incremental(void) {
    uint8_t hash1[32], hash2[32];
    
    /* One-shot */
    if (!plank_crypto_sha256((const uint8_t*)"hello world", 11, hash1)) {
        return 0;
    }
    
    /* Incremental */
    plank_sha256_ctx_t ctx;
    plank_sha256_init(&ctx);
    plank_sha256_update(&ctx, (const uint8_t*)"hello", 5);
    plank_sha256_update(&ctx, (const uint8_t*)" ", 1);
    plank_sha256_update(&ctx, (const uint8_t*)"world", 5);
    plank_sha256_final(&ctx, hash2);
    
    return memcmp(hash1, hash2, 32) == 0;
}

/* ============================================================================
 * ED25519 TESTS
 * ============================================================================ */

static int test_ed25519_keygen(void) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey, privkey)) {
        return 0;
    }
    
    /* Check that keys are not all zeros */
    int pub_nonzero = 0, priv_nonzero = 0;
    for (int i = 0; i < PLANK_PUBKEY_SIZE; i++) {
        if (pubkey[i] != 0) pub_nonzero = 1;
    }
    for (int i = 0; i < PLANK_PRIVKEY_SIZE; i++) {
        if (privkey[i] != 0) priv_nonzero = 1;
    }
    
    return pub_nonzero && priv_nonzero;
}

static int test_ed25519_sign_verify(void) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    uint8_t signature[PLANK_SIGNATURE_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey, privkey)) {
        return 0;
    }
    
    const uint8_t message[] = "test message for signing";
    size_t message_len = sizeof(message) - 1;
    
    /* Sign */
    if (!plank_crypto_sign_ed25519(message, message_len, privkey, signature)) {
        return 0;
    }
    
    /* Verify */
    if (!plank_crypto_verify_ed25519(message, message_len, signature, pubkey)) {
        return 0;
    }
    
    return 1;
}

static int test_ed25519_verify_tampered(void) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    uint8_t signature[PLANK_SIGNATURE_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey, privkey)) {
        return 0;
    }
    
    const uint8_t message[] = "test message for signing";
    size_t message_len = sizeof(message) - 1;
    
    if (!plank_crypto_sign_ed25519(message, message_len, privkey, signature)) {
        return 0;
    }
    
    /* Tamper with message */
    uint8_t tampered[] = "test message for signing!";
    
    /* Verification should fail */
    if (plank_crypto_verify_ed25519(tampered, sizeof(tampered) - 1, signature, pubkey)) {
        return 0;  /* Should have failed */
    }
    
    return 1;
}

static int test_ed25519_verify_wrong_key(void) {
    uint8_t pubkey1[PLANK_PUBKEY_SIZE], pubkey2[PLANK_PUBKEY_SIZE];
    uint8_t privkey1[PLANK_PRIVKEY_SIZE], privkey2[PLANK_PRIVKEY_SIZE];
    uint8_t signature[PLANK_SIGNATURE_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey1, privkey1)) return 0;
    if (!plank_crypto_keygen_ed25519(pubkey2, privkey2)) return 0;
    
    const uint8_t message[] = "test message";
    
    /* Sign with key 1 */
    if (!plank_crypto_sign_ed25519(message, sizeof(message) - 1, privkey1, signature)) {
        return 0;
    }
    
    /* Verify with key 2 should fail */
    if (plank_crypto_verify_ed25519(message, sizeof(message) - 1, signature, pubkey2)) {
        return 0;  /* Should have failed */
    }
    
    return 1;
}

/* ============================================================================
 * RANDOM TESTS
 * ============================================================================ */

static int test_random_bytes(void) {
    uint8_t buf1[32], buf2[32];
    
    if (!plank_crypto_random(buf1, sizeof(buf1))) return 0;
    if (!plank_crypto_random(buf2, sizeof(buf2))) return 0;
    
    /* Should be different */
    return memcmp(buf1, buf2, sizeof(buf1)) != 0;
}

static int test_random_node_id(void) {
    uint8_t id1[PLANK_NODE_ID_SIZE], id2[PLANK_NODE_ID_SIZE];
    
    if (!plank_crypto_random_node_id(id1)) return 0;
    if (!plank_crypto_random_node_id(id2)) return 0;
    
    return memcmp(id1, id2, PLANK_NODE_ID_SIZE) != 0;
}

static int test_random_nonce(void) {
    uint8_t nonce1[PLANK_NONCE_SIZE], nonce2[PLANK_NONCE_SIZE];
    
    if (!plank_crypto_random_nonce(nonce1)) return 0;
    if (!plank_crypto_random_nonce(nonce2)) return 0;
    
    return memcmp(nonce1, nonce2, PLANK_NONCE_SIZE) != 0;
}

/* ============================================================================
 * HEX CONVERSION TESTS
 * ============================================================================ */

static int test_hex_roundtrip(void) {
    uint8_t original[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    char hex[17];
    uint8_t decoded[8];
    
    plank_crypto_to_hex(original, sizeof(original), hex);
    
    if (strcmp(hex, "0123456789abcdef") != 0) {
        return 0;
    }
    
    if (!plank_crypto_from_hex(hex, decoded, sizeof(decoded))) {
        return 0;
    }
    
    return memcmp(original, decoded, sizeof(original)) == 0;
}

/* ============================================================================
 * AUTH TRANSCRIPT TESTS
 * ============================================================================ */

static int test_auth_transcript(void) {
    uint8_t local_id[PLANK_NODE_ID_SIZE];
    uint8_t remote_id[PLANK_NODE_ID_SIZE];
    uint8_t local_link_id[PLANK_LINK_ID_SIZE];
    uint8_t local_nonce[PLANK_NONCE_SIZE];
    uint8_t remote_nonce[PLANK_NONCE_SIZE];
    
    memset(local_id, 0x11, sizeof(local_id));
    memset(remote_id, 0x22, sizeof(remote_id));
    memset(local_link_id, 0x55, sizeof(local_link_id));
    memset(local_nonce, 0x33, sizeof(local_nonce));
    memset(remote_nonce, 0x44, sizeof(remote_nonce));
    
    uint64_t local_ts = 1000000;
    uint64_t remote_ts = 1000001;
    
    uint8_t transcript[256];
    size_t transcript_len;
    
    if (!plank_crypto_build_auth_transcript(
            local_id, remote_id, local_link_id,
            local_nonce, remote_nonce,
            local_ts, remote_ts,
            transcript, &transcript_len)) {
        return 0;
    }
    
    /* Verify transcript starts with "PLANK-AUTH-v1" */
    if (transcript_len < 13) return 0;
    if (memcmp(transcript, "PLANK-AUTH-v1", 13) != 0) return 0;
    
    /* Verify determinism */
    uint8_t transcript2[256];
    size_t transcript2_len;
    
    if (!plank_crypto_build_auth_transcript(
            local_id, remote_id, local_link_id,
            local_nonce, remote_nonce,
            local_ts, remote_ts,
            transcript2, &transcript2_len)) {
        return 0;
    }
    
    if (transcript_len != transcript2_len) return 0;
    if (memcmp(transcript, transcript2, transcript_len) != 0) return 0;
    
    return 1;
}

/* ============================================================================
 * KEY FINGERPRINT TESTS
 * ============================================================================ */

static int test_key_fingerprint(void) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey, privkey)) return 0;
    
    uint8_t fingerprint1[PLANK_OBJECT_ID_SIZE];
    uint8_t fingerprint2[PLANK_OBJECT_ID_SIZE];
    
    if (!plank_crypto_key_fingerprint(pubkey, fingerprint1)) return 0;
    if (!plank_crypto_key_fingerprint(pubkey, fingerprint2)) return 0;
    
    /* Same key should produce same fingerprint */
    return memcmp(fingerprint1, fingerprint2, PLANK_OBJECT_ID_SIZE) == 0;
}

/* ============================================================================
 * CONSTANT-TIME TESTS
 * ============================================================================ */

static int test_secure_memcmp(void) {
    uint8_t a[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t b[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t c[] = {0x01, 0x02, 0x03, 0x05};
    
    if (!plank_crypto_memcmp(a, b, sizeof(a))) return 0;
    if (plank_crypto_memcmp(a, c, sizeof(a))) return 0;
    
    return 1;
}

/* ============================================================================
 * ZSTD TESTS (if available)
 * ============================================================================ */

#ifdef HAVE_ZSTD
static int test_zstd_roundtrip(void) {
    const char* original = "This is test data for compression. "
                          "It should compress well because it has "
                          "repeated patterns and common words.";
    size_t original_len = strlen(original);
    
    /* Compress */
    uint8_t* compressed = NULL;
    size_t comp_len;
    if (!plank_crypto_zstd_compress((const uint8_t*)original, original_len,
                                     &compressed, &comp_len, 3)) {
        return 0;
    }
    
    /* Decompress */
    uint8_t* decompressed = NULL;
    size_t decomp_len;
    if (!plank_crypto_zstd_decompress(compressed, comp_len,
                                       &decompressed, &decomp_len, original_len * 2)) {
        plank_crypto_free_buffer(compressed);
        return 0;
    }
    
    int result = (decomp_len == original_len &&
                  memcmp(original, decompressed, original_len) == 0);
    
    plank_crypto_free_buffer(compressed);
    plank_crypto_free_buffer(decompressed);
    return result;
}
#endif

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("PLANK Crypto Tests\n");
    printf("==================\n\n");
    
    printf("SHA-256 tests:\n");
    TEST(sha256_empty);
    TEST(sha256_hello);
    TEST(sha256_incremental);
    
    printf("\nEd25519 tests:\n");
    TEST(ed25519_keygen);
    TEST(ed25519_sign_verify);
    TEST(ed25519_verify_tampered);
    TEST(ed25519_verify_wrong_key);
    
    printf("\nRandom tests:\n");
    TEST(random_bytes);
    TEST(random_node_id);
    TEST(random_nonce);
    
    printf("\nHex conversion tests:\n");
    TEST(hex_roundtrip);
    
    printf("\nAuth transcript tests:\n");
    TEST(auth_transcript);
    
    printf("\nKey fingerprint tests:\n");
    TEST(key_fingerprint);
    
    printf("\nConstant-time tests:\n");
    TEST(secure_memcmp);
    
#ifdef HAVE_ZSTD
    printf("\nZSTD tests:\n");
    TEST(zstd_roundtrip);
#else
    printf("\nZSTD tests: (skipped - not available)\n");
#endif
    
    printf("\n==================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    return tests_passed == tests_run ? 0 : 1;
}
