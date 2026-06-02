/*
 * PLANK Object Encoding/Decoding
 * Canonical CBOR serialization for PLANK objects
 */

#include "plank/plank.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * MESSAGE BODY ENCODING
 * ============================================================================ */

static bool encode_message_body(cbor_encoder_t* enc, const plank_message_body_t* body) {
    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);
    
    /* Encode each field into temporary buffers, then build sorted map */
    cbor_encoder_t tmp;
    uint8_t buf[8192];
    
    /* message_type */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->message_type);
    cbor_map_builder_add(&mb, "message_type", buf, tmp.len);
    
    /* author_user */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, body->author_user);
    cbor_map_builder_add(&mb, "author_user", buf, tmp.len);
    
    /* author_display (optional) */
    if (body->author_display[0]) {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_text(&tmp, body->author_display);
        cbor_map_builder_add(&mb, "author_display", buf, tmp.len);
    }
    
    /* from_addr */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, body->from_addr);
    cbor_map_builder_add(&mb, "from_addr", buf, tmp.len);
    
    /* to_addrs */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_array(&tmp, body->to_addrs_count);
    for (size_t i = 0; i < body->to_addrs_count; i++) {
        cbor_encode_text(&tmp, body->to_addrs[i]);
    }
    cbor_map_builder_add(&mb, "to_addrs", buf, tmp.len);
    
    /* area_addr (for AREA_POST) */
    if (body->message_type == PLANK_MSG_AREA_POST && body->area_addr[0]) {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_text(&tmp, body->area_addr);
        cbor_map_builder_add(&mb, "area_addr", buf, tmp.len);
    }
    
    /* subject */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, body->subject);
    cbor_map_builder_add(&mb, "subject", buf, tmp.len);
    
    /* body_format */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->body_format);
    cbor_map_builder_add(&mb, "body_format", buf, tmp.len);
    
    /* body_text */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text_len(&tmp, body->body_text, body->body_text_len);
    cbor_map_builder_add(&mb, "body_text", buf, tmp.len);
    
    /* thread_root_id (optional) */
    if (!plank_object_id_is_zero(body->thread_root_id)) {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_bytes(&tmp, body->thread_root_id, PLANK_OBJECT_ID_SIZE);
        cbor_map_builder_add(&mb, "thread_root_id", buf, tmp.len);
    }
    
    /* parent_id (optional) */
    if (!plank_object_id_is_zero(body->parent_id)) {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_bytes(&tmp, body->parent_id, PLANK_OBJECT_ID_SIZE);
        cbor_map_builder_add(&mb, "parent_id", buf, tmp.len);
    }
    
    /* reply_to (optional) */
    if (body->reply_to[0]) {
        cbor_encoder_init(&tmp, buf, sizeof(buf));
        cbor_encode_text(&tmp, body->reply_to);
        cbor_map_builder_add(&mb, "reply_to", buf, tmp.len);
    }
    
    /* attachment_refs */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_array(&tmp, body->attachment_refs_count);
    for (size_t i = 0; i < body->attachment_refs_count; i++) {
        cbor_encode_bytes(&tmp, body->attachment_refs[i], PLANK_ATTACHMENT_ID_SIZE);
    }
    cbor_map_builder_add(&mb, "attachment_refs", buf, tmp.len);
    
    /* retention_class */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->retention_class);
    cbor_map_builder_add(&mb, "retention_class", buf, tmp.len);
    
    /* visibility */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->visibility);
    cbor_map_builder_add(&mb, "visibility", buf, tmp.len);
    
    /* flags */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->flags);
    cbor_map_builder_add(&mb, "flags", buf, tmp.len);
    
    /* path */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_array(&tmp, body->path_count);
    for (size_t i = 0; i < body->path_count; i++) {
        cbor_encode_bytes(&tmp, body->path[i], PLANK_NODE_ID_SIZE);
    }
    cbor_map_builder_add(&mb, "path", buf, tmp.len);
    
    /* hop_count */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, body->hop_count);
    cbor_map_builder_add(&mb, "hop_count", buf, tmp.len);
    
    /* Encode sorted map */
    cbor_map_builder_encode(&mb, enc);
    cbor_map_builder_free(&mb);
    
    return cbor_encoder_ok(enc);
}

/* ============================================================================
 * OBJECT ENVELOPE ENCODING
 * ============================================================================ */

static bool encode_envelope_without_sig(cbor_encoder_t* enc, const plank_object_t* obj) {
    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);
    
    cbor_encoder_t tmp;
    uint8_t buf[256];
    
    /* version */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->version);
    cbor_map_builder_add(&mb, "version", buf, tmp.len);
    
    /* class */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->object_class);
    cbor_map_builder_add(&mb, "class", buf, tmp.len);
    
    /* origin_node_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, obj->origin_node_id, PLANK_NODE_ID_SIZE);
    cbor_map_builder_add(&mb, "origin_node_id", buf, tmp.len);
    
    /* origin_node_addr */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, obj->origin_node_addr);
    cbor_map_builder_add(&mb, "origin_node_addr", buf, tmp.len);
    
    /* created_at */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->created_at);
    cbor_map_builder_add(&mb, "created_at", buf, tmp.len);
    
    /* object_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, obj->object_id, PLANK_OBJECT_ID_SIZE);
    cbor_map_builder_add(&mb, "object_id", buf, tmp.len);
    
    /* body (raw CBOR) */
    cbor_map_builder_add(&mb, "body", obj->body_cbor, obj->body_cbor_len);
    
    /* sig_alg */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->sig_alg);
    cbor_map_builder_add(&mb, "sig_alg", buf, tmp.len);
    
    /* Encode sorted map (without signature) */
    cbor_map_builder_encode(&mb, enc);
    cbor_map_builder_free(&mb);
    
    return cbor_encoder_ok(enc);
}

static bool encode_envelope_with_sig(cbor_encoder_t* enc, const plank_object_t* obj) {
    cbor_map_builder_t mb;
    cbor_map_builder_init(&mb);
    
    cbor_encoder_t tmp;
    uint8_t buf[256];
    
    /* version */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->version);
    cbor_map_builder_add(&mb, "version", buf, tmp.len);
    
    /* class */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->object_class);
    cbor_map_builder_add(&mb, "class", buf, tmp.len);
    
    /* origin_node_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, obj->origin_node_id, PLANK_NODE_ID_SIZE);
    cbor_map_builder_add(&mb, "origin_node_id", buf, tmp.len);
    
    /* origin_node_addr */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_text(&tmp, obj->origin_node_addr);
    cbor_map_builder_add(&mb, "origin_node_addr", buf, tmp.len);
    
    /* created_at */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->created_at);
    cbor_map_builder_add(&mb, "created_at", buf, tmp.len);
    
    /* object_id */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, obj->object_id, PLANK_OBJECT_ID_SIZE);
    cbor_map_builder_add(&mb, "object_id", buf, tmp.len);
    
    /* body (raw CBOR) */
    cbor_map_builder_add(&mb, "body", obj->body_cbor, obj->body_cbor_len);
    
    /* sig_alg */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_uint(&tmp, obj->sig_alg);
    cbor_map_builder_add(&mb, "sig_alg", buf, tmp.len);
    
    /* signature */
    cbor_encoder_init(&tmp, buf, sizeof(buf));
    cbor_encode_bytes(&tmp, obj->signature, PLANK_SIGNATURE_SIZE);
    cbor_map_builder_add(&mb, "signature", buf, tmp.len);
    
    /* Encode sorted map */
    cbor_map_builder_encode(&mb, enc);
    cbor_map_builder_free(&mb);
    
    return cbor_encoder_ok(enc);
}

/* ============================================================================
 * OBJECT ENCODING
 * ============================================================================ */

bool plank_object_encode(plank_object_t* obj) {
    if (!obj || !obj->body_cbor) {
        plank_set_error("Invalid object for encoding");
        return false;
    }
    
    /* Free existing envelope */
    free(obj->envelope_cbor);
    obj->envelope_cbor = NULL;
    obj->envelope_cbor_len = 0;
    
    /* Encode with signature */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 4096);
    
    if (!encode_envelope_with_sig(&enc, obj)) {
        cbor_encoder_free(&enc);
        plank_set_error("Failed to encode object envelope");
        return false;
    }
    
    obj->envelope_cbor = (uint8_t*)enc.buf;
    obj->envelope_cbor_len = enc.len;
    
    return true;
}

/* ============================================================================
 * OBJECT ID COMPUTATION
 * ============================================================================ */

bool plank_object_compute_id(plank_object_t* obj) {
    if (!obj || !obj->body_cbor) {
        plank_set_error("Invalid object for ID computation");
        return false;
    }
    
    /* Step 1: Encode envelope without signature and without object_id */
    /* First, temporarily zero the object_id */
    uint8_t saved_id[PLANK_OBJECT_ID_SIZE];
    memcpy(saved_id, obj->object_id, PLANK_OBJECT_ID_SIZE);
    memset(obj->object_id, 0, PLANK_OBJECT_ID_SIZE);
    
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 4096);
    
    if (!encode_envelope_without_sig(&enc, obj)) {
        cbor_encoder_free(&enc);
        memcpy(obj->object_id, saved_id, PLANK_OBJECT_ID_SIZE);
        plank_set_error("Failed to encode for ID computation");
        return false;
    }
    
    /* Step 2: Hash to get object_id */
    if (!plank_crypto_sha256(enc.buf, enc.len, obj->object_id)) {
        cbor_encoder_free(&enc);
        memcpy(obj->object_id, saved_id, PLANK_OBJECT_ID_SIZE);
        plank_set_error("Failed to hash for object ID");
        return false;
    }
    
    cbor_encoder_free(&enc);
    return true;
}

/* ============================================================================
 * OBJECT SIGNING
 * ============================================================================ */

bool plank_object_sign(plank_object_t* obj, const uint8_t* signing_key_priv) {
    if (!obj || !obj->body_cbor || !signing_key_priv) {
        plank_set_error("Invalid arguments for signing");
        return false;
    }
    
    /* Compute object ID first */
    if (!plank_object_compute_id(obj)) {
        return false;
    }
    
    /* Encode to-be-signed bytes (envelope without signature) */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 4096);
    
    if (!encode_envelope_without_sig(&enc, obj)) {
        cbor_encoder_free(&enc);
        plank_set_error("Failed to encode for signing");
        return false;
    }
    
    /* Sign */
    if (!plank_crypto_sign_ed25519(enc.buf, enc.len, signing_key_priv, obj->signature)) {
        cbor_encoder_free(&enc);
        plank_set_error("Failed to sign object");
        return false;
    }
    
    cbor_encoder_free(&enc);
    
    /* Now encode full envelope with signature */
    return plank_object_encode(obj);
}

/* ============================================================================
 * OBJECT VERIFICATION
 * ============================================================================ */

bool plank_object_verify(const plank_object_t* obj, const uint8_t* signing_key_pub) {
    if (!obj || !obj->body_cbor || !signing_key_pub) {
        plank_set_error("Invalid arguments for verification");
        return false;
    }
    
    /* Create a copy to compute to-be-signed bytes */
    plank_object_t* tmp = plank_object_clone(obj);
    if (!tmp) {
        plank_set_error("Failed to clone object for verification");
        return false;
    }
    
    /* Encode to-be-signed bytes */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 4096);
    
    if (!encode_envelope_without_sig(&enc, tmp)) {
        cbor_encoder_free(&enc);
        plank_object_free(tmp);
        plank_set_error("Failed to encode for verification");
        return false;
    }
    
    /* Verify signature */
    bool valid = plank_crypto_verify_ed25519(enc.buf, enc.len,
                                             obj->signature, signing_key_pub);
    
    cbor_encoder_free(&enc);
    plank_object_free(tmp);
    
    if (!valid) {
        plank_set_error("Signature verification failed");
    }
    
    return valid;
}

bool plank_object_verify_id(const plank_object_t* obj) {
    if (!obj || !obj->body_cbor) {
        return false;
    }
    
    /* Create a copy and recompute ID */
    plank_object_t* tmp = plank_object_clone(obj);
    if (!tmp) return false;
    
    /* Save original ID */
    uint8_t original_id[PLANK_OBJECT_ID_SIZE];
    memcpy(original_id, obj->object_id, PLANK_OBJECT_ID_SIZE);
    
    /* Compute ID */
    if (!plank_object_compute_id(tmp)) {
        plank_object_free(tmp);
        return false;
    }
    
    /* Compare */
    bool match = plank_object_id_equal(original_id, tmp->object_id);
    plank_object_free(tmp);
    
    return match;
}

/* ============================================================================
 * OBJECT CREATION HELPERS
 * ============================================================================ */

plank_object_t* plank_object_create_message(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_message_body_t* body,
    const uint8_t* signing_key_priv)
{
    if (!origin_node_id || !origin_node_addr || !body || !signing_key_priv) {
        plank_set_error("Invalid arguments for message creation");
        return NULL;
    }
    
    plank_object_t* obj = plank_object_new();
    if (!obj) {
        plank_set_error("Failed to allocate object");
        return NULL;
    }
    
    obj->object_class = PLANK_CLASS_MESSAGE;
    memcpy(obj->origin_node_id, origin_node_id, PLANK_NODE_ID_SIZE);
    strncpy(obj->origin_node_addr, origin_node_addr, sizeof(obj->origin_node_addr) - 1);
    obj->created_at = (uint64_t)time(NULL);
    
    /* Encode body */
    cbor_encoder_t enc;
    cbor_encoder_init_dynamic(&enc, 8192);
    
    if (!encode_message_body(&enc, body)) {
        cbor_encoder_free(&enc);
        plank_object_free(obj);
        plank_set_error("Failed to encode message body");
        return NULL;
    }
    
    obj->body_cbor = enc.buf;
    obj->body_cbor_len = enc.len;
    
    /* Sign */
    if (!plank_object_sign(obj, signing_key_priv)) {
        plank_object_free(obj);
        return NULL;
    }
    
    return obj;
}

/* ============================================================================
 * OBJECT DECODING
 * ============================================================================ */

plank_object_t* plank_object_decode(const uint8_t* cbor, size_t len) {
    if (!cbor || len == 0) {
        plank_set_error("Invalid CBOR data");
        return NULL;
    }
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, cbor, len);
    
    size_t map_count = cbor_decode_map(&dec);
    if (!cbor_decoder_ok(&dec)) {
        plank_set_error("Failed to decode object map");
        return NULL;
    }
    
    plank_object_t* obj = plank_object_new();
    if (!obj) {
        plank_set_error("Failed to allocate object");
        return NULL;
    }
    
    /* Store full envelope */
    obj->envelope_cbor = malloc(len);
    if (!obj->envelope_cbor) {
        plank_object_free(obj);
        plank_set_error("Failed to allocate envelope buffer");
        return NULL;
    }
    memcpy(obj->envelope_cbor, cbor, len);
    obj->envelope_cbor_len = len;
    
    /* Parse map entries */
    for (size_t i = 0; i < map_count && cbor_decoder_ok(&dec); i++) {
        size_t key_len;
        const char* key = cbor_decode_text(&dec, &key_len);
        if (!key) break;
        
        if (key_len == 7 && memcmp(key, "version", 7) == 0) {
            obj->version = (uint16_t)cbor_decode_uint(&dec);
        } else if (key_len == 5 && memcmp(key, "class", 5) == 0) {
            obj->object_class = (plank_object_class_t)cbor_decode_uint(&dec);
        } else if (key_len == 14 && memcmp(key, "origin_node_id", 14) == 0) {
            size_t id_len;
            const uint8_t* id = cbor_decode_bytes(&dec, &id_len);
            if (id && id_len == PLANK_NODE_ID_SIZE) {
                memcpy(obj->origin_node_id, id, PLANK_NODE_ID_SIZE);
            }
        } else if (key_len == 16 && memcmp(key, "origin_node_addr", 16) == 0) {
            cbor_decode_text_copy(&dec, obj->origin_node_addr,
                                  sizeof(obj->origin_node_addr));
        } else if (key_len == 10 && memcmp(key, "created_at", 10) == 0) {
            obj->created_at = cbor_decode_uint(&dec);
        } else if (key_len == 9 && memcmp(key, "object_id", 9) == 0) {
            size_t id_len;
            const uint8_t* id = cbor_decode_bytes(&dec, &id_len);
            if (id && id_len == PLANK_OBJECT_ID_SIZE) {
                memcpy(obj->object_id, id, PLANK_OBJECT_ID_SIZE);
            }
        } else if (key_len == 4 && memcmp(key, "body", 4) == 0) {
            /* Record body position and skip */
            size_t body_start = dec.pos;
            cbor_skip(&dec);
            size_t body_end = dec.pos;
            
            obj->body_cbor_len = body_end - body_start;
            obj->body_cbor = malloc(obj->body_cbor_len);
            if (obj->body_cbor) {
                memcpy(obj->body_cbor, cbor + body_start, obj->body_cbor_len);
            }
        } else if (key_len == 7 && memcmp(key, "sig_alg", 7) == 0) {
            obj->sig_alg = (plank_sig_alg_t)cbor_decode_uint(&dec);
        } else if (key_len == 9 && memcmp(key, "signature", 9) == 0) {
            size_t sig_len;
            const uint8_t* sig = cbor_decode_bytes(&dec, &sig_len);
            if (sig && sig_len == PLANK_SIGNATURE_SIZE) {
                memcpy(obj->signature, sig, PLANK_SIGNATURE_SIZE);
            }
        } else {
            cbor_skip(&dec);
        }
    }
    
    if (!cbor_decoder_ok(&dec)) {
        plank_object_free(obj);
        plank_set_error("Failed to decode object");
        return NULL;
    }
    
    return obj;
}

/* ============================================================================
 * MESSAGE BODY DECODING
 * ============================================================================ */

plank_message_body_t* plank_object_decode_message_body(const plank_object_t* obj) {
    if (!obj || !obj->body_cbor || obj->object_class != PLANK_CLASS_MESSAGE) {
        plank_set_error("Invalid object for message body decoding");
        return NULL;
    }
    
    cbor_decoder_t dec;
    cbor_decoder_init(&dec, obj->body_cbor, obj->body_cbor_len);
    
    size_t map_count = cbor_decode_map(&dec);
    if (!cbor_decoder_ok(&dec)) {
        plank_set_error("Failed to decode message body map");
        return NULL;
    }
    
    plank_message_body_t* body = plank_message_body_new();
    if (!body) {
        plank_set_error("Failed to allocate message body");
        return NULL;
    }
    
    for (size_t i = 0; i < map_count && cbor_decoder_ok(&dec); i++) {
        size_t key_len;
        const char* key = cbor_decode_text(&dec, &key_len);
        if (!key) break;
        
        if (key_len == 12 && memcmp(key, "message_type", 12) == 0) {
            body->message_type = (plank_message_type_t)cbor_decode_uint(&dec);
        } else if (key_len == 11 && memcmp(key, "author_user", 11) == 0) {
            cbor_decode_text_copy(&dec, body->author_user, sizeof(body->author_user));
        } else if (key_len == 14 && memcmp(key, "author_display", 14) == 0) {
            cbor_decode_text_copy(&dec, body->author_display, sizeof(body->author_display));
        } else if (key_len == 9 && memcmp(key, "from_addr", 9) == 0) {
            cbor_decode_text_copy(&dec, body->from_addr, sizeof(body->from_addr));
        } else if (key_len == 8 && memcmp(key, "to_addrs", 8) == 0) {
            size_t arr_count = cbor_decode_array(&dec);
            if (arr_count > 0) {
                body->to_addrs = calloc(arr_count, sizeof(char*));
                if (body->to_addrs) {
                    for (size_t j = 0; j < arr_count && cbor_decoder_ok(&dec); j++) {
                        size_t addr_len;
                        const char* addr = cbor_decode_text(&dec, &addr_len);
                        if (addr) {
                            body->to_addrs[j] = strndup(addr, addr_len);
                        }
                    }
                    body->to_addrs_count = arr_count;
                }
            }
        } else if (key_len == 9 && memcmp(key, "area_addr", 9) == 0) {
            cbor_decode_text_copy(&dec, body->area_addr, sizeof(body->area_addr));
        } else if (key_len == 7 && memcmp(key, "subject", 7) == 0) {
            cbor_decode_text_copy(&dec, body->subject, sizeof(body->subject));
        } else if (key_len == 11 && memcmp(key, "body_format", 11) == 0) {
            body->body_format = (plank_body_format_t)cbor_decode_uint(&dec);
        } else if (key_len == 9 && memcmp(key, "body_text", 9) == 0) {
            size_t text_len;
            const char* text = cbor_decode_text(&dec, &text_len);
            if (text) {
                body->body_text = strndup(text, text_len);
                body->body_text_len = text_len;
            }
        } else if (key_len == 14 && memcmp(key, "thread_root_id", 14) == 0) {
            size_t id_len;
            const uint8_t* id = cbor_decode_bytes(&dec, &id_len);
            if (id && id_len == PLANK_OBJECT_ID_SIZE) {
                memcpy(body->thread_root_id, id, PLANK_OBJECT_ID_SIZE);
            }
        } else if (key_len == 9 && memcmp(key, "parent_id", 9) == 0) {
            size_t id_len;
            const uint8_t* id = cbor_decode_bytes(&dec, &id_len);
            if (id && id_len == PLANK_OBJECT_ID_SIZE) {
                memcpy(body->parent_id, id, PLANK_OBJECT_ID_SIZE);
            }
        } else if (key_len == 15 && memcmp(key, "retention_class", 15) == 0) {
            body->retention_class = (plank_retention_class_t)cbor_decode_uint(&dec);
        } else if (key_len == 10 && memcmp(key, "visibility", 10) == 0) {
            body->visibility = (plank_visibility_t)cbor_decode_uint(&dec);
        } else if (key_len == 5 && memcmp(key, "flags", 5) == 0) {
            body->flags = (uint32_t)cbor_decode_uint(&dec);
        } else if (key_len == 9 && memcmp(key, "hop_count", 9) == 0) {
            body->hop_count = (uint32_t)cbor_decode_uint(&dec);
        } else if (key_len == 4 && memcmp(key, "path", 4) == 0) {
            size_t arr_count = cbor_decode_array(&dec);
            if (arr_count > 0) {
                body->path = calloc(arr_count, sizeof(uint8_t*));
                if (body->path) {
                    for (size_t j = 0; j < arr_count && cbor_decoder_ok(&dec); j++) {
                        size_t id_len;
                        const uint8_t* id = cbor_decode_bytes(&dec, &id_len);
                        if (id && id_len == PLANK_NODE_ID_SIZE) {
                            body->path[j] = malloc(PLANK_NODE_ID_SIZE);
                            if (body->path[j]) {
                                memcpy(body->path[j], id, PLANK_NODE_ID_SIZE);
                            }
                        }
                    }
                    body->path_count = arr_count;
                }
            }
        } else {
            cbor_skip(&dec);
        }
    }
    
    if (!cbor_decoder_ok(&dec)) {
        plank_message_body_free(body);
        plank_set_error("Failed to decode message body");
        return NULL;
    }
    
    return body;
}

/* ============================================================================
 * OBJECT VALIDATION
 * ============================================================================ */

bool plank_object_validate(const plank_object_t* obj, char* error_buf, size_t error_len) {
    if (!obj) {
        if (error_buf) snprintf(error_buf, error_len, "Object is NULL");
        return false;
    }
    
    if (obj->version != 1) {
        if (error_buf) snprintf(error_buf, error_len, "Unsupported version: %d", obj->version);
        return false;
    }
    
    if (obj->object_class < PLANK_CLASS_AREA_DEFINITION ||
        obj->object_class > PLANK_CLASS_NODE_INFO) {
        if (error_buf) snprintf(error_buf, error_len, "Invalid object class: %d", obj->object_class);
        return false;
    }
    
    if (!obj->origin_node_addr[0]) {
        if (error_buf) snprintf(error_buf, error_len, "Missing origin node address");
        return false;
    }
    
    if (obj->created_at == 0) {
        if (error_buf) snprintf(error_buf, error_len, "Missing creation timestamp");
        return false;
    }
    
    if (!obj->body_cbor || obj->body_cbor_len == 0) {
        if (error_buf) snprintf(error_buf, error_len, "Missing body");
        return false;
    }
    
    if (obj->sig_alg != PLANK_SIG_ED25519) {
        if (error_buf) snprintf(error_buf, error_len, "Unsupported signature algorithm");
        return false;
    }
    
    return true;
}
