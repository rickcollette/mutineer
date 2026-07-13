/*
 * PLANK Cryptographic Operations
 * Ed25519 signing, SHA-256 hashing, key management
 */

#include "plank/plank_crypto.h"
#include "plank/plank_types.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

/* ============================================================================
 * KEY GENERATION
 * ============================================================================ */

bool plank_crypto_keygen_ed25519(uint8_t* pubkey_out, uint8_t* privkey_out) {
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    
    if (!ctx) return false;
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    /* Extract public key */
    size_t pub_len = PLANK_PUBKEY_SIZE;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey_out, &pub_len) <= 0) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    /* Extract private key */
    size_t priv_len = PLANK_PUBKEY_SIZE;  /* Ed25519 seed is 32 bytes */
    uint8_t seed[32];
    if (EVP_PKEY_get_raw_private_key(pkey, seed, &priv_len) <= 0) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    /* Store seed + pubkey as "private key" (64 bytes) */
    memcpy(privkey_out, seed, 32);
    memcpy(privkey_out + 32, pubkey_out, 32);
    
    plank_crypto_memzero(seed, sizeof(seed));
    EVP_PKEY_free(pkey);
    return true;
}

bool plank_crypto_keygen_x25519(uint8_t* pubkey_out, uint8_t* privkey_out) {
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    
    if (!ctx) return false;
    
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    
    EVP_PKEY_CTX_free(ctx);
    
    size_t pub_len = PLANK_PUBKEY_SIZE;
    if (EVP_PKEY_get_raw_public_key(pkey, pubkey_out, &pub_len) <= 0) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    size_t priv_len = PLANK_PUBKEY_SIZE;
    if (EVP_PKEY_get_raw_private_key(pkey, privkey_out, &priv_len) <= 0) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    EVP_PKEY_free(pkey);
    return true;
}

bool plank_crypto_pubkey_from_privkey(const uint8_t* privkey, uint8_t* pubkey_out) {
    /* For our format, pubkey is stored in bytes 32-63 of privkey */
    memcpy(pubkey_out, privkey + 32, 32);
    return true;
}

/* ============================================================================
 * ED25519 SIGNING
 * ============================================================================ */

bool plank_crypto_sign_ed25519(const uint8_t* message, size_t message_len,
                               const uint8_t* privkey, uint8_t* signature_out) {
    /* Create key from seed (first 32 bytes) */
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL,
                                                   privkey, 32);
    if (!pkey) return false;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    
    size_t sig_len = PLANK_SIGNATURE_SIZE;
    if (EVP_DigestSign(ctx, signature_out, &sig_len, message, message_len) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return true;
}

bool plank_crypto_verify_ed25519(const uint8_t* message, size_t message_len,
                                 const uint8_t* signature, const uint8_t* pubkey) {
    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL,
                                                  pubkey, PLANK_PUBKEY_SIZE);
    if (!pkey) return false;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return false;
    }
    
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    
    int result = EVP_DigestVerify(ctx, signature, PLANK_SIGNATURE_SIZE,
                                  message, message_len);
    
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result == 1;
}

/* ============================================================================
 * SHA-256 HASHING
 * ============================================================================ */

bool plank_crypto_sha256(const uint8_t* data, size_t len, uint8_t* hash_out) {
    unsigned int out_len = 0;
    return EVP_Digest(data, len, hash_out, &out_len, EVP_sha256(), NULL) == 1 &&
           out_len == PLANK_HASH_SIZE;
}

void plank_sha256_init(plank_sha256_ctx_t* ctx) {
    if (!ctx) return;
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (md) EVP_DigestInit_ex(md, EVP_sha256(), NULL);
    memset(ctx->state, 0, sizeof(ctx->state));
    memcpy(ctx->state, &md, sizeof(md));
}

void plank_sha256_update(plank_sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (!ctx) return;
    EVP_MD_CTX* md = NULL;
    memcpy(&md, ctx->state, sizeof(md));
    if (md) EVP_DigestUpdate(md, data, len);
}

void plank_sha256_final(plank_sha256_ctx_t* ctx, uint8_t* hash_out) {
    if (!ctx || !hash_out) return;
    EVP_MD_CTX* md = NULL;
    unsigned int out_len = 0;
    memcpy(&md, ctx->state, sizeof(md));
    if (md) {
        EVP_DigestFinal_ex(md, hash_out, &out_len);
        EVP_MD_CTX_free(md);
    }
    memset(ctx->state, 0, sizeof(ctx->state));
}

/* ============================================================================
 * RANDOM NUMBER GENERATION
 * ============================================================================ */

bool plank_crypto_random(uint8_t* buf, size_t len) {
    return RAND_bytes(buf, (int)len) == 1;
}

bool plank_crypto_random_node_id(uint8_t* node_id_out) {
    return plank_crypto_random(node_id_out, PLANK_NODE_ID_SIZE);
}

bool plank_crypto_random_link_id(uint8_t* link_id_out) {
    return plank_crypto_random(link_id_out, PLANK_LINK_ID_SIZE);
}

bool plank_crypto_random_export_id(uint8_t* export_id_out) {
    return plank_crypto_random(export_id_out, PLANK_EXPORT_ID_SIZE);
}

bool plank_crypto_random_nonce(uint8_t* nonce_out) {
    return plank_crypto_random(nonce_out, PLANK_NONCE_SIZE);
}

/* ============================================================================
 * KEY FINGERPRINTING
 * ============================================================================ */

bool plank_crypto_key_fingerprint(const uint8_t* pubkey, uint8_t* fingerprint_out) {
    return plank_crypto_sha256(pubkey, PLANK_PUBKEY_SIZE, fingerprint_out);
}

void plank_crypto_fingerprint_to_hex(const uint8_t* fingerprint, char* hex_out) {
    plank_crypto_to_hex(fingerprint, PLANK_HASH_SIZE, hex_out);
}

/* ============================================================================
 * AUTH TRANSCRIPT
 * ============================================================================ */

#define AUTH_TRANSCRIPT_PREFIX "PLANK-AUTH-v1"
#define AUTH_TRANSCRIPT_MAX_SIZE (13 + 16 + 16 + 16 + 32 + 32 + 8 + 8)

bool plank_crypto_build_auth_transcript(
    const uint8_t* initiator_node_id,
    const uint8_t* responder_node_id,
    const uint8_t* initiator_link_id,
    const uint8_t* initiator_nonce,
    const uint8_t* responder_nonce,
    uint64_t initiator_timestamp,
    uint64_t responder_timestamp,
    uint8_t* transcript_out,
    size_t* transcript_len_out)
{
    uint8_t* p = transcript_out;
    
    /* Prefix */
    memcpy(p, AUTH_TRANSCRIPT_PREFIX, 13);
    p += 13;
    
    /* Node IDs */
    memcpy(p, initiator_node_id, PLANK_NODE_ID_SIZE);
    p += PLANK_NODE_ID_SIZE;
    
    memcpy(p, responder_node_id, PLANK_NODE_ID_SIZE);
    p += PLANK_NODE_ID_SIZE;
    
    /* Link ID */
    memcpy(p, initiator_link_id, PLANK_LINK_ID_SIZE);
    p += PLANK_LINK_ID_SIZE;
    
    /* Nonces */
    memcpy(p, initiator_nonce, PLANK_NONCE_SIZE);
    p += PLANK_NONCE_SIZE;
    
    memcpy(p, responder_nonce, PLANK_NONCE_SIZE);
    p += PLANK_NONCE_SIZE;
    
    /* Timestamps (big-endian) */
    for (int i = 7; i >= 0; i--) {
        *p++ = (uint8_t)(initiator_timestamp >> (i * 8));
    }
    for (int i = 7; i >= 0; i--) {
        *p++ = (uint8_t)(responder_timestamp >> (i * 8));
    }
    
    *transcript_len_out = p - transcript_out;
    return true;
}

bool plank_crypto_sign_auth_transcript(const uint8_t* transcript, size_t transcript_len,
                                       const uint8_t* privkey, uint8_t* signature_out) {
    return plank_crypto_sign_ed25519(transcript, transcript_len, privkey, signature_out);
}

bool plank_crypto_verify_auth_transcript(const uint8_t* transcript, size_t transcript_len,
                                         const uint8_t* signature, const uint8_t* pubkey) {
    return plank_crypto_verify_ed25519(transcript, transcript_len, signature, pubkey);
}

/* ============================================================================
 * ZSTD COMPRESSION
 * ============================================================================ */

bool plank_crypto_zstd_compress(const uint8_t* input, size_t input_len,
                                uint8_t** output, size_t* output_len,
                                int compression_level) {
#ifdef HAVE_ZSTD
    size_t bound = ZSTD_compressBound(input_len);
    uint8_t* buf = malloc(bound);
    if (!buf) return false;
    
    size_t result = ZSTD_compress(buf, bound, input, input_len, compression_level);
    if (ZSTD_isError(result)) {
        free(buf);
        return false;
    }
    
    *output = buf;
    *output_len = result;
    return true;
#else
    (void)input; (void)input_len; (void)output; (void)output_len;
    (void)compression_level;
    return false;
#endif
}

bool plank_crypto_zstd_decompress(const uint8_t* input, size_t input_len,
                                  uint8_t** output, size_t* output_len,
                                  size_t max_output_len) {
#ifdef HAVE_ZSTD
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(input, input_len);
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return false;
    }
    
    if (decompressed_size > max_output_len) {
        return false;
    }
    
    uint8_t* buf = malloc((size_t)decompressed_size);
    if (!buf) return false;
    
    size_t result = ZSTD_decompress(buf, (size_t)decompressed_size, input, input_len);
    if (ZSTD_isError(result)) {
        free(buf);
        return false;
    }
    
    *output = buf;
    *output_len = result;
    return true;
#else
    (void)input; (void)input_len; (void)output; (void)output_len;
    (void)max_output_len;
    return false;
#endif
}

void plank_crypto_free_buffer(uint8_t* buf) {
    free(buf);
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

bool plank_crypto_memcmp(const uint8_t* a, const uint8_t* b, size_t len) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

void plank_crypto_memzero(void* ptr, size_t len) {
    volatile uint8_t* p = (volatile uint8_t*)ptr;
    while (len--) {
        *p++ = 0;
    }
}

static const char hex_chars[] = "0123456789abcdef";

void plank_crypto_to_hex(const uint8_t* bytes, size_t len, char* hex_out) {
    for (size_t i = 0; i < len; i++) {
        hex_out[i * 2] = hex_chars[(bytes[i] >> 4) & 0xF];
        hex_out[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
    }
    hex_out[len * 2] = '\0';
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool plank_crypto_from_hex(const char* hex, uint8_t* bytes_out, size_t max_len) {
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return false;
    
    size_t byte_len = hex_len / 2;
    if (byte_len > max_len) return false;
    
    for (size_t i = 0; i < byte_len; i++) {
        int hi = hex_digit(hex[i * 2]);
        int lo = hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes_out[i] = (uint8_t)((hi << 4) | lo);
    }
    
    return true;
}
