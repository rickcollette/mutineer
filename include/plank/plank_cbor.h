/*
 * PLANK CBOR Encoding/Decoding
 * Packet Link for Area Networked Knowledge
 *
 * Canonical CBOR implementation for PLANK objects and payloads.
 * Implements deterministic encoding as required by the spec.
 */

#ifndef PLANK_CBOR_H
#define PLANK_CBOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CBOR TYPES
 * ============================================================================ */

typedef enum {
    CBOR_TYPE_UINT     = 0,
    CBOR_TYPE_NEGINT   = 1,
    CBOR_TYPE_BYTES    = 2,
    CBOR_TYPE_TEXT     = 3,
    CBOR_TYPE_ARRAY    = 4,
    CBOR_TYPE_MAP      = 5,
    CBOR_TYPE_TAG      = 6,
    CBOR_TYPE_SIMPLE   = 7
} cbor_type_t;

/* Simple values */
#define CBOR_FALSE  20
#define CBOR_TRUE   21
#define CBOR_NULL   22
#define CBOR_UNDEF  23

/* ============================================================================
 * CBOR ENCODER
 * ============================================================================ */

typedef struct {
    uint8_t* buf;
    size_t   cap;
    size_t   len;
    bool     error;
} cbor_encoder_t;

/* Initialize encoder with buffer */
void cbor_encoder_init(cbor_encoder_t* enc, uint8_t* buf, size_t cap);

/* Initialize encoder with dynamic allocation */
void cbor_encoder_init_dynamic(cbor_encoder_t* enc, size_t initial_cap);

/* Free dynamic encoder buffer */
void cbor_encoder_free(cbor_encoder_t* enc);

/* Get encoded bytes */
const uint8_t* cbor_encoder_data(const cbor_encoder_t* enc);
size_t cbor_encoder_len(const cbor_encoder_t* enc);

/* Check for encoding errors */
bool cbor_encoder_ok(const cbor_encoder_t* enc);

/* ============================================================================
 * ENCODING FUNCTIONS
 * ============================================================================ */

/* Encode unsigned integer */
void cbor_encode_uint(cbor_encoder_t* enc, uint64_t val);

/* Encode negative integer (val represents -1 - val) */
void cbor_encode_negint(cbor_encoder_t* enc, uint64_t val);

/* Encode signed integer */
void cbor_encode_int(cbor_encoder_t* enc, int64_t val);

/* Encode byte string */
void cbor_encode_bytes(cbor_encoder_t* enc, const uint8_t* data, size_t len);

/* Encode text string (UTF-8) */
void cbor_encode_text(cbor_encoder_t* enc, const char* str);

/* Encode text string with explicit length */
void cbor_encode_text_len(cbor_encoder_t* enc, const char* str, size_t len);

/* Encode array header (caller must encode elements) */
void cbor_encode_array(cbor_encoder_t* enc, size_t count);

/* Encode map header (caller must encode key-value pairs) */
void cbor_encode_map(cbor_encoder_t* enc, size_t count);

/* Encode boolean */
void cbor_encode_bool(cbor_encoder_t* enc, bool val);

/* Encode null */
void cbor_encode_null(cbor_encoder_t* enc);

/* Encode raw bytes directly */
void cbor_encode_raw(cbor_encoder_t* enc, const uint8_t* data, size_t len);

/* ============================================================================
 * CANONICAL MAP ENCODING
 *
 * For deterministic encoding, map keys must be sorted by:
 * 1. Length of encoded key (shorter first)
 * 2. Lexicographic order of encoded bytes
 *
 * These helpers manage sorted key insertion.
 * ============================================================================ */

typedef struct {
    const char* key;
    uint8_t*    encoded_value;
    size_t      encoded_value_len;
} cbor_map_entry_t;

typedef struct {
    cbor_map_entry_t* entries;
    size_t            count;
    size_t            cap;
} cbor_map_builder_t;

/* Initialize map builder */
void cbor_map_builder_init(cbor_map_builder_t* mb);

/* Free map builder */
void cbor_map_builder_free(cbor_map_builder_t* mb);

/* Add entry to map builder (value is copied) */
bool cbor_map_builder_add(cbor_map_builder_t* mb, const char* key,
                          const uint8_t* encoded_value, size_t len);

/* Encode map builder contents in canonical order */
void cbor_map_builder_encode(cbor_map_builder_t* mb, cbor_encoder_t* enc);

/* ============================================================================
 * CBOR DECODER
 * ============================================================================ */

typedef struct {
    const uint8_t* buf;
    size_t         len;
    size_t         pos;
    bool           error;
} cbor_decoder_t;

/* Initialize decoder */
void cbor_decoder_init(cbor_decoder_t* dec, const uint8_t* buf, size_t len);

/* Check for decoding errors */
bool cbor_decoder_ok(const cbor_decoder_t* dec);

/* Check if at end of input */
bool cbor_decoder_eof(const cbor_decoder_t* dec);

/* Get current position */
size_t cbor_decoder_pos(const cbor_decoder_t* dec);

/* ============================================================================
 * DECODING FUNCTIONS
 * ============================================================================ */

/* Peek at next type without consuming */
cbor_type_t cbor_peek_type(cbor_decoder_t* dec);

/* Decode unsigned integer */
uint64_t cbor_decode_uint(cbor_decoder_t* dec);

/* Decode signed integer */
int64_t cbor_decode_int(cbor_decoder_t* dec);

/* Decode byte string (returns pointer into buffer, sets len) */
const uint8_t* cbor_decode_bytes(cbor_decoder_t* dec, size_t* len_out);

/* Decode byte string into provided buffer */
bool cbor_decode_bytes_copy(cbor_decoder_t* dec, uint8_t* buf, size_t buf_cap, size_t* len_out);

/* Decode text string (returns pointer into buffer, sets len) */
const char* cbor_decode_text(cbor_decoder_t* dec, size_t* len_out);

/* Decode text string into provided buffer (null-terminated) */
bool cbor_decode_text_copy(cbor_decoder_t* dec, char* buf, size_t buf_cap);

/* Decode array header, returns element count */
size_t cbor_decode_array(cbor_decoder_t* dec);

/* Decode map header, returns pair count */
size_t cbor_decode_map(cbor_decoder_t* dec);

/* Decode boolean */
bool cbor_decode_bool(cbor_decoder_t* dec);

/* Skip one CBOR item (including nested structures) */
void cbor_skip(cbor_decoder_t* dec);

/* ============================================================================
 * MAP DECODING HELPERS
 * ============================================================================ */

/* Find key in map, returns true if found and positions decoder at value */
bool cbor_map_find_key(cbor_decoder_t* dec, const char* key, size_t pair_count);

/* Decode map and call handler for each key-value pair */
typedef bool (*cbor_map_handler_t)(const char* key, size_t key_len,
                                   cbor_decoder_t* value_dec, void* ctx);
bool cbor_decode_map_foreach(cbor_decoder_t* dec, cbor_map_handler_t handler, void* ctx);

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

/* Validate that buffer contains well-formed CBOR */
bool cbor_validate(const uint8_t* buf, size_t len);

/* Check if CBOR is in canonical form */
bool cbor_is_canonical(const uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_CBOR_H */
