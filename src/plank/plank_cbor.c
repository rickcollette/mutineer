/*
 * PLANK CBOR Implementation
 * Canonical CBOR encoding/decoding for PLANK protocol
 */

#include "plank/plank_cbor.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

static void encoder_ensure_space(cbor_encoder_t* enc, size_t needed) {
    if (enc->error) return;
    
    if (enc->buf == NULL) {
        /* Dynamic allocation mode */
        if (enc->len + needed > enc->cap) {
            size_t new_cap = enc->cap * 2;
            if (new_cap < enc->len + needed) {
                new_cap = enc->len + needed + 256;
            }
            uint8_t* new_buf = realloc(enc->buf, new_cap);
            if (!new_buf) {
                enc->error = true;
                return;
            }
            enc->buf = new_buf;
            enc->cap = new_cap;
        }
    } else {
        /* Fixed buffer mode */
        if (enc->len + needed > enc->cap) {
            enc->error = true;
        }
    }
}

static void encoder_write_byte(cbor_encoder_t* enc, uint8_t b) {
    encoder_ensure_space(enc, 1);
    if (!enc->error) {
        enc->buf[enc->len++] = b;
    }
}

static void encoder_write_bytes(cbor_encoder_t* enc, const uint8_t* data, size_t len) {
    encoder_ensure_space(enc, len);
    if (!enc->error) {
        memcpy(enc->buf + enc->len, data, len);
        enc->len += len;
    }
}

/* Encode type and argument in canonical form */
static void encode_type_arg(cbor_encoder_t* enc, uint8_t major, uint64_t arg) {
    uint8_t mt = major << 5;
    
    if (arg < 24) {
        encoder_write_byte(enc, mt | (uint8_t)arg);
    } else if (arg <= 0xFF) {
        encoder_write_byte(enc, mt | 24);
        encoder_write_byte(enc, (uint8_t)arg);
    } else if (arg <= 0xFFFF) {
        encoder_write_byte(enc, mt | 25);
        encoder_write_byte(enc, (uint8_t)(arg >> 8));
        encoder_write_byte(enc, (uint8_t)arg);
    } else if (arg <= 0xFFFFFFFF) {
        encoder_write_byte(enc, mt | 26);
        encoder_write_byte(enc, (uint8_t)(arg >> 24));
        encoder_write_byte(enc, (uint8_t)(arg >> 16));
        encoder_write_byte(enc, (uint8_t)(arg >> 8));
        encoder_write_byte(enc, (uint8_t)arg);
    } else {
        encoder_write_byte(enc, mt | 27);
        encoder_write_byte(enc, (uint8_t)(arg >> 56));
        encoder_write_byte(enc, (uint8_t)(arg >> 48));
        encoder_write_byte(enc, (uint8_t)(arg >> 40));
        encoder_write_byte(enc, (uint8_t)(arg >> 32));
        encoder_write_byte(enc, (uint8_t)(arg >> 24));
        encoder_write_byte(enc, (uint8_t)(arg >> 16));
        encoder_write_byte(enc, (uint8_t)(arg >> 8));
        encoder_write_byte(enc, (uint8_t)arg);
    }
}

/* ============================================================================
 * ENCODER IMPLEMENTATION
 * ============================================================================ */

void cbor_encoder_init(cbor_encoder_t* enc, uint8_t* buf, size_t cap) {
    enc->buf = buf;
    enc->cap = cap;
    enc->len = 0;
    enc->error = false;
}

void cbor_encoder_init_dynamic(cbor_encoder_t* enc, size_t initial_cap) {
    if (initial_cap < 64) initial_cap = 64;
    enc->buf = malloc(initial_cap);
    enc->cap = initial_cap;
    enc->len = 0;
    enc->error = (enc->buf == NULL);
}

void cbor_encoder_free(cbor_encoder_t* enc) {
    if (enc->buf) {
        free(enc->buf);
        enc->buf = NULL;
    }
    enc->cap = 0;
    enc->len = 0;
}

const uint8_t* cbor_encoder_data(const cbor_encoder_t* enc) {
    return enc->buf;
}

size_t cbor_encoder_len(const cbor_encoder_t* enc) {
    return enc->len;
}

bool cbor_encoder_ok(const cbor_encoder_t* enc) {
    return !enc->error;
}

void cbor_encode_uint(cbor_encoder_t* enc, uint64_t val) {
    encode_type_arg(enc, CBOR_TYPE_UINT, val);
}

void cbor_encode_negint(cbor_encoder_t* enc, uint64_t val) {
    encode_type_arg(enc, CBOR_TYPE_NEGINT, val);
}

void cbor_encode_int(cbor_encoder_t* enc, int64_t val) {
    if (val >= 0) {
        cbor_encode_uint(enc, (uint64_t)val);
    } else {
        cbor_encode_negint(enc, (uint64_t)(-(val + 1)));
    }
}

void cbor_encode_bytes(cbor_encoder_t* enc, const uint8_t* data, size_t len) {
    encode_type_arg(enc, CBOR_TYPE_BYTES, len);
    if (len > 0 && data) {
        encoder_write_bytes(enc, data, len);
    }
}

void cbor_encode_text(cbor_encoder_t* enc, const char* str) {
    size_t len = str ? strlen(str) : 0;
    cbor_encode_text_len(enc, str, len);
}

void cbor_encode_text_len(cbor_encoder_t* enc, const char* str, size_t len) {
    encode_type_arg(enc, CBOR_TYPE_TEXT, len);
    if (len > 0 && str) {
        encoder_write_bytes(enc, (const uint8_t*)str, len);
    }
}

void cbor_encode_array(cbor_encoder_t* enc, size_t count) {
    encode_type_arg(enc, CBOR_TYPE_ARRAY, count);
}

void cbor_encode_map(cbor_encoder_t* enc, size_t count) {
    encode_type_arg(enc, CBOR_TYPE_MAP, count);
}

void cbor_encode_bool(cbor_encoder_t* enc, bool val) {
    encoder_write_byte(enc, 0xF4 + (val ? 1 : 0));
}

void cbor_encode_null(cbor_encoder_t* enc) {
    encoder_write_byte(enc, 0xF6);
}

void cbor_encode_raw(cbor_encoder_t* enc, const uint8_t* data, size_t len) {
    encoder_write_bytes(enc, data, len);
}

/* ============================================================================
 * CANONICAL MAP BUILDER
 * ============================================================================ */

void cbor_map_builder_init(cbor_map_builder_t* mb) {
    mb->entries = NULL;
    mb->count = 0;
    mb->cap = 0;
}

void cbor_map_builder_free(cbor_map_builder_t* mb) {
    if (mb->entries) {
        for (size_t i = 0; i < mb->count; i++) {
            free(mb->entries[i].encoded_value);
        }
        free(mb->entries);
    }
    mb->entries = NULL;
    mb->count = 0;
    mb->cap = 0;
}

bool cbor_map_builder_add(cbor_map_builder_t* mb, const char* key,
                          const uint8_t* encoded_value, size_t len) {
    if (mb->count >= mb->cap) {
        size_t new_cap = mb->cap == 0 ? 8 : mb->cap * 2;
        cbor_map_entry_t* new_entries = realloc(mb->entries,
                                                new_cap * sizeof(cbor_map_entry_t));
        if (!new_entries) return false;
        mb->entries = new_entries;
        mb->cap = new_cap;
    }
    
    uint8_t* value_copy = malloc(len);
    if (!value_copy) return false;
    memcpy(value_copy, encoded_value, len);
    
    mb->entries[mb->count].key = key;
    mb->entries[mb->count].encoded_value = value_copy;
    mb->entries[mb->count].encoded_value_len = len;
    mb->count++;
    
    return true;
}

/* Compare function for canonical map key ordering */
static int compare_map_entries(const void* a, const void* b) {
    const cbor_map_entry_t* ea = (const cbor_map_entry_t*)a;
    const cbor_map_entry_t* eb = (const cbor_map_entry_t*)b;
    
    size_t len_a = strlen(ea->key);
    size_t len_b = strlen(eb->key);
    
    /* Shorter keys come first */
    if (len_a != len_b) {
        return (len_a < len_b) ? -1 : 1;
    }
    
    /* Same length: lexicographic order */
    return memcmp(ea->key, eb->key, len_a);
}

void cbor_map_builder_encode(cbor_map_builder_t* mb, cbor_encoder_t* enc) {
    /* Sort entries by canonical key order */
    if (mb->count > 1) {
        qsort(mb->entries, mb->count, sizeof(cbor_map_entry_t), compare_map_entries);
    }
    
    /* Encode map header */
    cbor_encode_map(enc, mb->count);
    
    /* Encode key-value pairs */
    for (size_t i = 0; i < mb->count; i++) {
        cbor_encode_text(enc, mb->entries[i].key);
        cbor_encode_raw(enc, mb->entries[i].encoded_value,
                        mb->entries[i].encoded_value_len);
    }
}

/* ============================================================================
 * DECODER IMPLEMENTATION
 * ============================================================================ */

void cbor_decoder_init(cbor_decoder_t* dec, const uint8_t* buf, size_t len) {
    dec->buf = buf;
    dec->len = len;
    dec->pos = 0;
    dec->error = false;
}

bool cbor_decoder_ok(const cbor_decoder_t* dec) {
    return !dec->error;
}

bool cbor_decoder_eof(const cbor_decoder_t* dec) {
    return dec->pos >= dec->len;
}

size_t cbor_decoder_pos(const cbor_decoder_t* dec) {
    return dec->pos;
}

static uint8_t decoder_read_byte(cbor_decoder_t* dec) {
    if (dec->error || dec->pos >= dec->len) {
        dec->error = true;
        return 0;
    }
    return dec->buf[dec->pos++];
}

static uint64_t decode_arg(cbor_decoder_t* dec, uint8_t info) {
    if (info < 24) {
        return info;
    } else if (info == 24) {
        return decoder_read_byte(dec);
    } else if (info == 25) {
        uint64_t val = decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        return val;
    } else if (info == 26) {
        uint64_t val = decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        return val;
    } else if (info == 27) {
        uint64_t val = decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        val = (val << 8) | decoder_read_byte(dec);
        return val;
    } else {
        dec->error = true;
        return 0;
    }
}

cbor_type_t cbor_peek_type(cbor_decoder_t* dec) {
    if (dec->error || dec->pos >= dec->len) {
        dec->error = true;
        return CBOR_TYPE_UINT;
    }
    return (cbor_type_t)(dec->buf[dec->pos] >> 5);
}

uint64_t cbor_decode_uint(cbor_decoder_t* dec) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return 0;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt != CBOR_TYPE_UINT) {
        dec->error = true;
        return 0;
    }
    
    return decode_arg(dec, info);
}

int64_t cbor_decode_int(cbor_decoder_t* dec) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return 0;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt == CBOR_TYPE_UINT) {
        return (int64_t)decode_arg(dec, info);
    } else if (mt == CBOR_TYPE_NEGINT) {
        uint64_t val = decode_arg(dec, info);
        return -1 - (int64_t)val;
    } else {
        dec->error = true;
        return 0;
    }
}

const uint8_t* cbor_decode_bytes(cbor_decoder_t* dec, size_t* len_out) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return NULL;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt != CBOR_TYPE_BYTES) {
        dec->error = true;
        return NULL;
    }
    
    uint64_t len = decode_arg(dec, info);
    if (dec->error) return NULL;
    
    if (dec->pos + len > dec->len) {
        dec->error = true;
        return NULL;
    }
    
    const uint8_t* data = dec->buf + dec->pos;
    dec->pos += len;
    *len_out = (size_t)len;
    return data;
}

bool cbor_decode_bytes_copy(cbor_decoder_t* dec, uint8_t* buf, size_t buf_cap,
                            size_t* len_out) {
    size_t len;
    const uint8_t* data = cbor_decode_bytes(dec, &len);
    if (!data) return false;
    
    if (len > buf_cap) {
        dec->error = true;
        return false;
    }
    
    memcpy(buf, data, len);
    *len_out = len;
    return true;
}

const char* cbor_decode_text(cbor_decoder_t* dec, size_t* len_out) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return NULL;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt != CBOR_TYPE_TEXT) {
        dec->error = true;
        return NULL;
    }
    
    uint64_t len = decode_arg(dec, info);
    if (dec->error) return NULL;
    
    if (dec->pos + len > dec->len) {
        dec->error = true;
        return NULL;
    }
    
    const char* text = (const char*)(dec->buf + dec->pos);
    dec->pos += len;
    *len_out = (size_t)len;
    return text;
}

bool cbor_decode_text_copy(cbor_decoder_t* dec, char* buf, size_t buf_cap) {
    size_t len;
    const char* text = cbor_decode_text(dec, &len);
    if (!text) return false;
    
    if (len >= buf_cap) {
        dec->error = true;
        return false;
    }
    
    memcpy(buf, text, len);
    buf[len] = '\0';
    return true;
}

size_t cbor_decode_array(cbor_decoder_t* dec) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return 0;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt != CBOR_TYPE_ARRAY) {
        dec->error = true;
        return 0;
    }
    
    return (size_t)decode_arg(dec, info);
}

size_t cbor_decode_map(cbor_decoder_t* dec) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return 0;
    
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    if (mt != CBOR_TYPE_MAP) {
        dec->error = true;
        return 0;
    }
    
    return (size_t)decode_arg(dec, info);
}

bool cbor_decode_bool(cbor_decoder_t* dec) {
    uint8_t b = decoder_read_byte(dec);
    if (dec->error) return false;
    
    if (b == 0xF4) return false;
    if (b == 0xF5) return true;
    
    dec->error = true;
    return false;
}

void cbor_skip(cbor_decoder_t* dec) {
    if (dec->error || dec->pos >= dec->len) {
        dec->error = true;
        return;
    }
    
    uint8_t b = decoder_read_byte(dec);
    uint8_t mt = b >> 5;
    uint8_t info = b & 0x1F;
    
    switch (mt) {
        case CBOR_TYPE_UINT:
        case CBOR_TYPE_NEGINT:
            decode_arg(dec, info);
            break;
            
        case CBOR_TYPE_BYTES:
        case CBOR_TYPE_TEXT: {
            uint64_t len = decode_arg(dec, info);
            if (!dec->error) {
                if (dec->pos + len > dec->len) {
                    dec->error = true;
                } else {
                    dec->pos += len;
                }
            }
            break;
        }
        
        case CBOR_TYPE_ARRAY: {
            uint64_t count = decode_arg(dec, info);
            for (uint64_t i = 0; i < count && !dec->error; i++) {
                cbor_skip(dec);
            }
            break;
        }
        
        case CBOR_TYPE_MAP: {
            uint64_t count = decode_arg(dec, info);
            for (uint64_t i = 0; i < count && !dec->error; i++) {
                cbor_skip(dec);  /* key */
                cbor_skip(dec);  /* value */
            }
            break;
        }
        
        case CBOR_TYPE_TAG:
            decode_arg(dec, info);
            cbor_skip(dec);
            break;
            
        case CBOR_TYPE_SIMPLE:
            if (info >= 24 && info <= 27) {
                decode_arg(dec, info);
            }
            break;
    }
}

bool cbor_map_find_key(cbor_decoder_t* dec, const char* key, size_t pair_count) {
    size_t key_len = strlen(key);
    size_t start_pos = dec->pos;
    
    for (size_t i = 0; i < pair_count && !dec->error; i++) {
        size_t found_len;
        const char* found_key = cbor_decode_text(dec, &found_len);
        if (!found_key) break;
        
        if (found_len == key_len && memcmp(found_key, key, key_len) == 0) {
            return true;  /* Positioned at value */
        }
        
        cbor_skip(dec);  /* Skip value */
    }
    
    /* Not found, restore position */
    dec->pos = start_pos;
    return false;
}

bool cbor_decode_map_foreach(cbor_decoder_t* dec, cbor_map_handler_t handler, void* ctx) {
    size_t count = cbor_decode_map(dec);
    if (dec->error) return false;
    
    for (size_t i = 0; i < count && !dec->error; i++) {
        size_t key_len;
        const char* key = cbor_decode_text(dec, &key_len);
        if (!key) return false;
        
        size_t value_start = dec->pos;
        cbor_skip(dec);
        if (dec->error) return false;
        size_t value_end = dec->pos;
        
        /* Create sub-decoder for value */
        cbor_decoder_t value_dec;
        cbor_decoder_init(&value_dec, dec->buf + value_start, value_end - value_start);
        
        if (!handler(key, key_len, &value_dec, ctx)) {
            return false;
        }
    }
    
    return !dec->error;
}

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

bool cbor_validate(const uint8_t* buf, size_t len) {
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, buf, len);
    cbor_skip(&dec);
    return cbor_decoder_ok(&dec) && cbor_decoder_eof(&dec);
}

bool cbor_is_canonical(const uint8_t* buf, size_t len) {
    /* For now, just validate structure */
    /* Full canonical validation would check:
     * - Shortest encoding for integers
     * - Map keys in sorted order
     * - No indefinite-length items
     */
    return cbor_validate(buf, len);
}
