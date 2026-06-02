/*
 * PLANK Cryptographic Operations
 * Packet Link for Area Networked Knowledge
 *
 * Ed25519 signing, SHA-256 hashing, and key management.
 */

#ifndef PLANK_CRYPTO_H
#define PLANK_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * KEY GENERATION
 * ============================================================================ */

/* Generate a new Ed25519 key pair */
bool plank_crypto_keygen_ed25519(uint8_t* pubkey_out, uint8_t* privkey_out);

/* Generate a new X25519 key pair (for key exchange) */
bool plank_crypto_keygen_x25519(uint8_t* pubkey_out, uint8_t* privkey_out);

/* Derive public key from private key */
bool plank_crypto_pubkey_from_privkey(const uint8_t* privkey, uint8_t* pubkey_out);

/* ============================================================================
 * ED25519 SIGNING
 * ============================================================================ */

/* Sign a message with Ed25519 */
bool plank_crypto_sign_ed25519(
    const uint8_t* message, size_t message_len,
    const uint8_t* privkey,
    uint8_t* signature_out
);

/* Verify an Ed25519 signature */
bool plank_crypto_verify_ed25519(
    const uint8_t* message, size_t message_len,
    const uint8_t* signature,
    const uint8_t* pubkey
);

/* ============================================================================
 * SHA-256 HASHING
 * ============================================================================ */

/* Compute SHA-256 hash of data */
bool plank_crypto_sha256(
    const uint8_t* data, size_t len,
    uint8_t* hash_out
);

/* Incremental SHA-256 context */
typedef struct {
    uint8_t state[128];  /* opaque state */
} plank_sha256_ctx_t;

/* Initialize SHA-256 context */
void plank_sha256_init(plank_sha256_ctx_t* ctx);

/* Update SHA-256 with more data */
void plank_sha256_update(plank_sha256_ctx_t* ctx, const uint8_t* data, size_t len);

/* Finalize SHA-256 and get hash */
void plank_sha256_final(plank_sha256_ctx_t* ctx, uint8_t* hash_out);

/* ============================================================================
 * RANDOM NUMBER GENERATION
 * ============================================================================ */

/* Generate cryptographically secure random bytes */
bool plank_crypto_random(uint8_t* buf, size_t len);

/* Generate a random node ID */
bool plank_crypto_random_node_id(uint8_t* node_id_out);

/* Generate a random link ID */
bool plank_crypto_random_link_id(uint8_t* link_id_out);

/* Generate a random export ID */
bool plank_crypto_random_export_id(uint8_t* export_id_out);

/* Generate a random nonce */
bool plank_crypto_random_nonce(uint8_t* nonce_out);

/* ============================================================================
 * KEY FINGERPRINTING
 * ============================================================================ */

/* Compute fingerprint (SHA-256) of public key */
bool plank_crypto_key_fingerprint(const uint8_t* pubkey, uint8_t* fingerprint_out);

/* Format fingerprint as hex string */
void plank_crypto_fingerprint_to_hex(const uint8_t* fingerprint, char* hex_out);

/* ============================================================================
 * AUTH TRANSCRIPT
 * ============================================================================ */

/*
 * Build the authentication transcript for AUTH_PROOF:
 * "PLANK-AUTH-v1" ||
 * initiator_node_id ||
 * responder_node_id ||
 * initiator_link_id ||
 * initiator_nonce ||
 * responder_nonce ||
 * initiator_timestamp ||
 * responder_timestamp
 */
bool plank_crypto_build_auth_transcript(
    const uint8_t* initiator_node_id,
    const uint8_t* responder_node_id,
    const uint8_t* initiator_link_id,
    const uint8_t* initiator_nonce,
    const uint8_t* responder_nonce,
    uint64_t initiator_timestamp,
    uint64_t responder_timestamp,
    uint8_t* transcript_out,
    size_t* transcript_len_out
);

/* Sign auth transcript */
bool plank_crypto_sign_auth_transcript(
    const uint8_t* transcript, size_t transcript_len,
    const uint8_t* privkey,
    uint8_t* signature_out
);

/* Verify auth transcript signature */
bool plank_crypto_verify_auth_transcript(
    const uint8_t* transcript, size_t transcript_len,
    const uint8_t* signature,
    const uint8_t* pubkey
);

/* ============================================================================
 * ZSTD COMPRESSION
 * ============================================================================ */

/* Compress data with zstd */
bool plank_crypto_zstd_compress(
    const uint8_t* input, size_t input_len,
    uint8_t** output, size_t* output_len,
    int compression_level
);

/* Decompress zstd data */
bool plank_crypto_zstd_decompress(
    const uint8_t* input, size_t input_len,
    uint8_t** output, size_t* output_len,
    size_t max_output_len
);

/* Free compressed/decompressed buffer */
void plank_crypto_free_buffer(uint8_t* buf);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* Constant-time memory comparison */
bool plank_crypto_memcmp(const uint8_t* a, const uint8_t* b, size_t len);

/* Secure memory zeroing */
void plank_crypto_memzero(void* ptr, size_t len);

/* Convert bytes to hex string */
void plank_crypto_to_hex(const uint8_t* bytes, size_t len, char* hex_out);

/* Convert hex string to bytes */
bool plank_crypto_from_hex(const char* hex, uint8_t* bytes_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_CRYPTO_H */
