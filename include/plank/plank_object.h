/*
 * PLANK Object Model
 * Packet Link for Area Networked Knowledge
 *
 * High-level object structures and manipulation functions.
 */

#ifndef PLANK_OBJECT_H
#define PLANK_OBJECT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * OBJECT ENVELOPE
 * ============================================================================ */

typedef struct {
    uint16_t version;
    plank_object_class_t object_class;
    uint8_t  origin_node_id[PLANK_NODE_ID_SIZE];
    char     origin_node_addr[PLANK_MAX_ADDRESS];
    uint64_t created_at;                            /* Unix timestamp */
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
    plank_sig_alg_t sig_alg;
    uint8_t  signature[PLANK_SIGNATURE_SIZE];
    
    /* Body is class-specific, stored separately */
    uint8_t* body_cbor;
    size_t   body_cbor_len;
    
    /* Full envelope CBOR (for storage/verification) */
    uint8_t* envelope_cbor;
    size_t   envelope_cbor_len;
} plank_object_t;

/* ============================================================================
 * MESSAGE OBJECT BODY
 * ============================================================================ */

typedef struct {
    plank_message_type_t message_type;
    char     author_user[PLANK_MAX_USER_NAME];
    char     author_display[PLANK_MAX_USER_NAME];
    char     from_addr[PLANK_MAX_ADDRESS];
    char**   to_addrs;                              /* NULL-terminated array */
    size_t   to_addrs_count;
    char     area_addr[PLANK_MAX_ADDRESS];          /* for AREA_POST */
    char     subject[PLANK_MAX_SUBJECT];
    plank_body_format_t body_format;
    char*    body_text;
    size_t   body_text_len;
    uint8_t  thread_root_id[PLANK_OBJECT_ID_SIZE];  /* may be zero */
    uint8_t  parent_id[PLANK_OBJECT_ID_SIZE];       /* may be zero */
    char     reply_to[PLANK_MAX_ADDRESS];
    uint8_t** attachment_refs;                      /* array of 32-byte IDs */
    size_t   attachment_refs_count;
    plank_retention_class_t retention_class;
    plank_visibility_t visibility;
    uint32_t flags;
    uint8_t** path;                                 /* array of 16-byte node IDs */
    size_t   path_count;
    uint32_t hop_count;
} plank_message_body_t;

/* ============================================================================
 * ATTACHMENT METADATA BODY
 * ============================================================================ */

typedef struct {
    uint8_t  attachment_id[PLANK_ATTACHMENT_ID_SIZE];
    char     filename[PLANK_MAX_FILENAME];
    char     mime_type[PLANK_MAX_MIME_TYPE];
    uint64_t size_bytes;
    uint8_t  sha256[PLANK_HASH_SIZE];
    plank_compression_t compression;
    plank_retention_class_t retention_class;
} plank_attachment_meta_body_t;

/* ============================================================================
 * AREA DEFINITION BODY
 * ============================================================================ */

typedef struct {
    char     area_addr[PLANK_MAX_ADDRESS];
    char     title[256];
    char     description[1024];
    char     origin_node_addr[PLANK_MAX_ADDRESS];
    plank_distribution_mode_t distribution_mode;
    plank_retention_class_t default_retention;
    plank_moderation_mode_t posting_policy;
    plank_moderation_mode_t attachment_policy;
    char     created_by[PLANK_MAX_ADDRESS];
    uint16_t status;
} plank_area_def_body_t;

/* ============================================================================
 * AREA POLICY BODY
 * ============================================================================ */

typedef struct {
    char     area_addr[PLANK_MAX_ADDRESS];
    plank_moderation_mode_t moderation_mode;
    char**   allowed_roles;
    size_t   allowed_roles_count;
    uint8_t** link_acl;                             /* array of link IDs */
    size_t   link_acl_count;
    uint32_t max_message_bytes;
    uint32_t max_attachment_bytes;
    uint16_t* allowed_body_formats;
    size_t   allowed_body_formats_count;
    uint32_t retention_days;
    bool     quarantine_on_violation;
    uint32_t duplicate_window_days;
    uint32_t max_hops;
} plank_area_policy_body_t;

/* ============================================================================
 * SUBSCRIPTION EVENT BODY
 * ============================================================================ */

typedef struct {
    char     area_addr[PLANK_MAX_ADDRESS];
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    plank_subscription_action_t action;
    uint64_t effective_at;
} plank_subscription_body_t;

/* ============================================================================
 * MODERATION EVENT BODY
 * ============================================================================ */

typedef struct {
    uint8_t  target_object_id[PLANK_OBJECT_ID_SIZE];
    char     area_addr[PLANK_MAX_ADDRESS];
    plank_moderation_action_t action;
    char     reason_code[64];
    char     reason_text[PLANK_MAX_REASON_TEXT];
    char     issued_by_node[PLANK_MAX_ADDRESS];
    char     issued_by_user[PLANK_MAX_USER_NAME];
    uint64_t effective_at;
    uint16_t visibility_scope;
} plank_moderation_body_t;

/* ============================================================================
 * ROUTING EVENT BODY
 * ============================================================================ */

typedef struct {
    uint8_t  target_object_id[PLANK_OBJECT_ID_SIZE];
    uint16_t route_type;
    uint8_t  next_hop_link_id[PLANK_LINK_ID_SIZE];
    uint8_t  path_append_node_id[PLANK_NODE_ID_SIZE];
    uint32_t hop_count;
    char*    details;
} plank_routing_body_t;

/* ============================================================================
 * RECEIPT EVENT BODY
 * ============================================================================ */

typedef struct {
    uint8_t  target_id[PLANK_OBJECT_ID_SIZE];       /* bundle or object ID */
    plank_target_kind_t target_kind;
    plank_receipt_type_t receipt_type;
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    uint64_t received_at;
    char*    details;
} plank_receipt_body_t;

/* ============================================================================
 * LINK EVENT BODY
 * ============================================================================ */

typedef struct {
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    uint16_t event_type;
    uint64_t effective_at;
    uint8_t  old_key_fingerprint[PLANK_HASH_SIZE];
    uint8_t  new_key_fingerprint[PLANK_HASH_SIZE];
    char*    notes;
} plank_link_event_body_t;

/* ============================================================================
 * BUNDLE CHECKPOINT BODY
 * ============================================================================ */

typedef struct {
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    uint16_t direction;
    uint64_t journal_seq_low;
    uint64_t journal_seq_high;
    uint64_t exported_at;
    uint64_t imported_at;
    char*    notes;
} plank_checkpoint_body_t;

/* ============================================================================
 * NODE INFO BODY
 * ============================================================================ */

typedef struct {
    uint8_t  node_id[PLANK_NODE_ID_SIZE];
    char     node_addr[PLANK_MAX_ADDRESS];
    char     software_name[64];
    char     software_version[32];
    uint64_t capabilities;
    uint32_t max_bundle_bytes;
    uint32_t max_frame_bytes;
    char*    description;
} plank_node_info_body_t;

/* ============================================================================
 * OBJECT LIFECYCLE FUNCTIONS
 * ============================================================================ */

/* Allocate and initialize an empty object */
plank_object_t* plank_object_new(void);

/* Free an object and all its contents */
void plank_object_free(plank_object_t* obj);

/* Deep copy an object */
plank_object_t* plank_object_clone(const plank_object_t* obj);

/* ============================================================================
 * MESSAGE BODY LIFECYCLE
 * ============================================================================ */

plank_message_body_t* plank_message_body_new(void);
void plank_message_body_free(plank_message_body_t* body);
plank_message_body_t* plank_message_body_clone(const plank_message_body_t* body);

/* ============================================================================
 * OBJECT CREATION HELPERS
 * ============================================================================ */

/* Create a message object from a message body */
plank_object_t* plank_object_create_message(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_message_body_t* body,
    const uint8_t* signing_key_priv
);

/* Create an attachment metadata object */
plank_object_t* plank_object_create_attachment_meta(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_attachment_meta_body_t* body,
    const uint8_t* signing_key_priv
);

/* Create an area definition object */
plank_object_t* plank_object_create_area_def(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_area_def_body_t* body,
    const uint8_t* signing_key_priv
);

/* Create a moderation event object */
plank_object_t* plank_object_create_moderation(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_moderation_body_t* body,
    const uint8_t* signing_key_priv
);

/* Create a subscription event object */
plank_object_t* plank_object_create_subscription(
    const uint8_t* origin_node_id,
    const char* origin_node_addr,
    const plank_subscription_body_t* body,
    const uint8_t* signing_key_priv
);

/* ============================================================================
 * OBJECT SERIALIZATION
 * ============================================================================ */

/* Encode object to canonical CBOR (sets envelope_cbor) */
bool plank_object_encode(plank_object_t* obj);

/* Decode object from canonical CBOR */
plank_object_t* plank_object_decode(const uint8_t* cbor, size_t len);

/* Decode message body from object */
plank_message_body_t* plank_object_decode_message_body(const plank_object_t* obj);

/* Decode attachment meta body from object */
plank_attachment_meta_body_t* plank_object_decode_attachment_meta_body(const plank_object_t* obj);

/* ============================================================================
 * OBJECT HASHING AND SIGNING
 * ============================================================================ */

/* Compute object ID (SHA-256 of canonical to-be-signed bytes) */
bool plank_object_compute_id(plank_object_t* obj);

/* Sign the object with Ed25519 */
bool plank_object_sign(plank_object_t* obj, const uint8_t* signing_key_priv);

/* Verify object signature */
bool plank_object_verify(const plank_object_t* obj, const uint8_t* signing_key_pub);

/* ============================================================================
 * OBJECT VALIDATION
 * ============================================================================ */

/* Validate object structure and required fields */
bool plank_object_validate(const plank_object_t* obj, char* error_buf, size_t error_len);

/* Check if object ID matches content */
bool plank_object_verify_id(const plank_object_t* obj);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/* Format object ID as hex string */
void plank_object_id_to_hex(const uint8_t* id, char* hex_out);

/* Parse hex string to object ID */
bool plank_hex_to_object_id(const char* hex, uint8_t* id_out);

/* Get object class name as string */
const char* plank_object_class_name(plank_object_class_t cls);

/* Get message type name as string */
const char* plank_message_type_name(plank_message_type_t type);

/* Check if two object IDs are equal */
bool plank_object_id_equal(const uint8_t* a, const uint8_t* b);

/* Check if object ID is all zeros */
bool plank_object_id_is_zero(const uint8_t* id);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_OBJECT_H */
