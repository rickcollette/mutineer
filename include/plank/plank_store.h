/*
 * PLANK Storage Layer
 * Packet Link for Area Networked Knowledge
 *
 * Database operations for PLANK objects, links, areas, and state.
 * Uses the Mutineer BBS database directly.
 */

#ifndef PLANK_STORE_H
#define PLANK_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"
#include "plank_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct BbsDb BbsDb;

/* ============================================================================
 * PLANK STORE CONTEXT
 * ============================================================================ */

typedef struct plank_store plank_store_t;

/* Open PLANK store (wraps BbsDb) */
plank_store_t* plank_store_open(BbsDb* db);

/* Close PLANK store */
void plank_store_close(plank_store_t* store);

/* Get underlying BbsDb */
BbsDb* plank_store_get_db(plank_store_t* store);

/* Initialize PLANK schema (run migrations) */
bool plank_store_init_schema(plank_store_t* store, const char* schema_path);

/* ============================================================================
 * NODE IDENTITY
 * ============================================================================ */

typedef struct {
    uint8_t  node_id[PLANK_NODE_ID_SIZE];
    char     node_name[PLANK_MAX_NODE_NAME];
    char     network_name[PLANK_MAX_NETWORK_NAME];
    char     node_addr[PLANK_MAX_ADDRESS];
    uint8_t  signing_key_pub[PLANK_PUBKEY_SIZE];
    uint8_t  signing_key_priv[PLANK_PRIVKEY_SIZE];
    uint8_t  link_key_pub[PLANK_PUBKEY_SIZE];
    uint8_t  link_key_priv[PLANK_PRIVKEY_SIZE];
    char     software_name[64];
    char     software_version[32];
    bool     is_cove;
    uint32_t max_bundle_bytes;
    uint32_t max_frame_bytes;
    uint32_t poll_interval_sec;
} plank_node_identity_t;

/* Get local node identity */
bool plank_store_get_identity(plank_store_t* store, plank_node_identity_t* out);

/* Set local node identity (creates or updates) */
bool plank_store_set_identity(plank_store_t* store, const plank_node_identity_t* identity);

/* Generate new node identity */
bool plank_store_generate_identity(plank_store_t* store,
                                   const char* node_name,
                                   const char* network_name);

/* ============================================================================
 * PEER MANAGEMENT
 * ============================================================================ */

typedef struct {
    int      id;
    uint8_t  node_id[PLANK_NODE_ID_SIZE];
    char     node_name[PLANK_MAX_NODE_NAME];
    char     network_name[PLANK_MAX_NETWORK_NAME];
    char     node_addr[PLANK_MAX_ADDRESS];
    uint8_t  signing_key_pub[PLANK_PUBKEY_SIZE];
    char     tls_fingerprint[65];
    char     first_seen_at[32];
    char     last_seen_at[32];
    int      trust_level;
    int      status;
    char     notes[256];
} plank_peer_t;

/* Get peer by node address */
bool plank_store_peer_get(plank_store_t* store, const char* node_addr, plank_peer_t* out);

/* Get peer by node ID */
bool plank_store_peer_get_by_id(plank_store_t* store, const uint8_t* node_id, plank_peer_t* out);

/* Add or update peer */
bool plank_store_peer_upsert(plank_store_t* store, const plank_peer_t* peer);

/* List all peers */
int plank_store_peer_list(plank_store_t* store, plank_peer_t* out, int max);

/* Update peer last seen */
bool plank_store_peer_touch(plank_store_t* store, int peer_id);

/* ============================================================================
 * LINK MANAGEMENT
 * ============================================================================ */

typedef struct {
    int      id;
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    char     link_name[64];
    int      peer_id;
    char     remote_host[256];
    int      remote_port;
    plank_link_direction_t direction;
    bool     enabled;
    bool     paused;
    int      retry_initial_sec;
    int      retry_max_sec;
    int      retry_limit;
    char     last_connect_at[32];
    char     last_success_at[32];
    char     last_error[256];
    int      retry_count;
    plank_link_state_t state;
} plank_link_t;

/* Get link by ID */
bool plank_store_link_get(plank_store_t* store, int link_id, plank_link_t* out);

/* Get link by link_id bytes */
bool plank_store_link_get_by_link_id(plank_store_t* store, const uint8_t* link_id, plank_link_t* out);

/* Add link */
bool plank_store_link_add(plank_store_t* store, const plank_link_t* link, int* id_out);

/* Update link */
bool plank_store_link_update(plank_store_t* store, const plank_link_t* link);

/* List all links */
int plank_store_link_list(plank_store_t* store, plank_link_t* out, int max);

/* List enabled links */
int plank_store_link_list_enabled(plank_store_t* store, plank_link_t* out, int max);

/* Update link state */
bool plank_store_link_set_state(plank_store_t* store, int link_id, plank_link_state_t state);

/* Update link error */
bool plank_store_link_set_error(plank_store_t* store, int link_id, const char* error);

/* Increment link retry count */
bool plank_store_link_inc_retry(plank_store_t* store, int link_id);

/* Reset link retry count */
bool plank_store_link_reset_retry(plank_store_t* store, int link_id);

/* ============================================================================
 * LINK CURSORS
 * ============================================================================ */

/* Get export cursor for link */
bool plank_store_cursor_get_export(plank_store_t* store, int link_id, uint64_t* seq_out);

/* Get import cursor for link */
bool plank_store_cursor_get_import(plank_store_t* store, int link_id, uint64_t* seq_out);

/* Set export cursor for link */
bool plank_store_cursor_set_export(plank_store_t* store, int link_id, uint64_t seq);

/* Set import cursor for link */
bool plank_store_cursor_set_import(plank_store_t* store, int link_id, uint64_t seq);

/* ============================================================================
 * AREA MANAGEMENT
 * ============================================================================ */

typedef struct {
    int      id;
    char     area_addr[PLANK_MAX_ADDRESS];
    char     area_slug[PLANK_MAX_AREA_SLUG];
    char     origin_node_addr[PLANK_MAX_ADDRESS];
    char     title[256];
    char     description[1024];
    plank_distribution_mode_t distribution_mode;
    plank_retention_class_t default_retention;
    plank_moderation_mode_t posting_policy;
    plank_moderation_mode_t attachment_policy;
    uint32_t max_message_bytes;
    uint32_t max_attachment_bytes;
    uint32_t retention_days;
    uint32_t max_hops;
    int      status;
    int      local_area_id;     /* link to BBS message_areas */
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
} plank_area_t;

/* Get area by address */
bool plank_store_area_get(plank_store_t* store, const char* area_addr, plank_area_t* out);

/* Get area by ID */
bool plank_store_area_get_by_id(plank_store_t* store, int area_id, plank_area_t* out);

/* Add or update area */
bool plank_store_area_upsert(plank_store_t* store, const plank_area_t* area, int* id_out);

/* List all areas */
int plank_store_area_list(plank_store_t* store, plank_area_t* out, int max);

/* Map area to local BBS message area */
bool plank_store_area_set_local(plank_store_t* store, int area_id, int local_area_id);

/* ============================================================================
 * SUBSCRIPTION MANAGEMENT
 * ============================================================================ */

typedef struct {
    int      id;
    int      link_id;
    int      area_id;
    plank_subscription_action_t action;
    char     effective_at[32];
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
} plank_subscription_t;

/* Get subscription for link and area */
bool plank_store_subscription_get(plank_store_t* store, int link_id, int area_id,
                                  plank_subscription_t* out);

/* Set subscription */
bool plank_store_subscription_set(plank_store_t* store, const plank_subscription_t* sub);

/* List subscriptions for link */
int plank_store_subscription_list_by_link(plank_store_t* store, int link_id,
                                          plank_subscription_t* out, int max);

/* List subscriptions for area */
int plank_store_subscription_list_by_area(plank_store_t* store, int area_id,
                                          plank_subscription_t* out, int max);

/* Check if link is subscribed to area */
bool plank_store_subscription_is_active(plank_store_t* store, int link_id, int area_id);

/* ============================================================================
 * OBJECT STORAGE
 * ============================================================================ */

/* Store object (returns false if duplicate) */
bool plank_store_object_put(plank_store_t* store, const plank_object_t* obj,
                            plank_source_kind_t source, int source_link_id,
                            int64_t* local_seq_out);

/* Get object by ID */
plank_object_t* plank_store_object_get(plank_store_t* store, const uint8_t* object_id);

/* Check if object exists */
bool plank_store_object_exists(plank_store_t* store, const uint8_t* object_id);

/* Get objects since journal sequence */
int plank_store_objects_since(plank_store_t* store, uint64_t since_seq,
                              plank_object_t** out, int max);

/* Get objects for area since sequence */
int plank_store_objects_for_area(plank_store_t* store, const char* area_addr,
                                 uint64_t since_seq, plank_object_t** out, int max);

/* ============================================================================
 * MESSAGE STORAGE (denormalized)
 * ============================================================================ */

typedef struct {
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
    plank_message_type_t message_type;
    char     author_user[PLANK_MAX_USER_NAME];
    char     author_display[PLANK_MAX_USER_NAME];
    char     from_addr[PLANK_MAX_ADDRESS];
    char     area_addr[PLANK_MAX_ADDRESS];
    char     subject[PLANK_MAX_SUBJECT];
    plank_body_format_t body_format;
    char*    body_text;
    uint8_t  thread_root_id[PLANK_OBJECT_ID_SIZE];
    uint8_t  parent_id[PLANK_OBJECT_ID_SIZE];
    plank_visibility_t visibility;
    uint32_t flags;
    uint32_t hop_count;
    int      local_msg_id;      /* link to BBS messages table */
} plank_message_record_t;

/* Store message record */
bool plank_store_message_put(plank_store_t* store, const plank_message_record_t* msg);

/* Get message by object ID */
bool plank_store_message_get(plank_store_t* store, const uint8_t* object_id,
                             plank_message_record_t* out);

/* Link message to local BBS message */
bool plank_store_message_set_local(plank_store_t* store, const uint8_t* object_id,
                                   int local_msg_id);

/* ============================================================================
 * ATTACHMENT STORAGE
 * ============================================================================ */

typedef struct {
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
    uint8_t  attachment_id[PLANK_ATTACHMENT_ID_SIZE];
    char     filename[PLANK_MAX_FILENAME];
    char     mime_type[PLANK_MAX_MIME_TYPE];
    uint64_t size_bytes;
    uint8_t  sha256[PLANK_HASH_SIZE];
    plank_compression_t compression;
    plank_retention_class_t retention_class;
    char     blob_path[512];
} plank_attachment_record_t;

/* Store attachment metadata */
bool plank_store_attachment_put(plank_store_t* store, const plank_attachment_record_t* att);

/* Get attachment by attachment ID */
bool plank_store_attachment_get(plank_store_t* store, const uint8_t* attachment_id,
                                plank_attachment_record_t* out);

/* Set attachment blob path */
bool plank_store_attachment_set_path(plank_store_t* store, const uint8_t* attachment_id,
                                     const char* blob_path);

/* ============================================================================
 * JOURNAL
 * ============================================================================ */

/* Get current journal sequence */
uint64_t plank_store_journal_seq(plank_store_t* store);

/* Get journal entries since sequence */
typedef struct {
    uint64_t local_seq;
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
    plank_object_class_t object_class;
    char     accepted_at[32];
    plank_source_kind_t source_kind;
    int      source_link_id;
    int      processing_state;
} plank_journal_entry_t;

int plank_store_journal_list(plank_store_t* store, uint64_t since_seq,
                             plank_journal_entry_t* out, int max);

/* ============================================================================
 * IMPORT HISTORY AND DEDUPLICATION
 * ============================================================================ */

/* Record bundle import */
bool plank_store_import_record(plank_store_t* store,
                               const uint8_t* bundle_id,
                               plank_bundle_type_t bundle_type,
                               const uint8_t* source_node_id,
                               const char* source_node_addr,
                               int source_link_id,
                               plank_import_result_t result,
                               int object_count,
                               int duplicate_count,
                               int rejected_count);

/* Check if bundle was already imported */
bool plank_store_import_exists(plank_store_t* store, const uint8_t* bundle_id);

/* Record object seen (for dedup) */
bool plank_store_dedupe_record(plank_store_t* store, const uint8_t* object_id);

/* Check if object was already seen */
bool plank_store_dedupe_exists(plank_store_t* store, const uint8_t* object_id);

/* Prune old dedup entries */
int plank_store_dedupe_prune(plank_store_t* store, int days_old);

/* ============================================================================
 * DEAD LETTERS AND QUARANTINE
 * ============================================================================ */

/* Add to dead letter queue */
bool plank_store_deadletter_add(plank_store_t* store,
                                int target_link_id,
                                const char* target_node_addr,
                                const uint8_t** object_ids,
                                int object_count,
                                const uint8_t* bundle_id,
                                int error_code,
                                const char* error_text);

/* List dead letters */
typedef struct {
    int      id;
    int      target_link_id;
    char     target_node_addr[PLANK_MAX_ADDRESS];
    char     first_failure_at[32];
    char     last_failure_at[32];
    int      last_error_code;
    char     last_error_text[256];
    int      retry_count;
    int      state;
} plank_deadletter_t;

int plank_store_deadletter_list(plank_store_t* store, plank_deadletter_t* out, int max);

/* Requeue dead letter */
bool plank_store_deadletter_requeue(plank_store_t* store, int id);

/* Abandon dead letter */
bool plank_store_deadletter_abandon(plank_store_t* store, int id);

/* Add to quarantine */
bool plank_store_quarantine_add(plank_store_t* store,
                                const uint8_t* object_id,
                                plank_object_class_t object_class,
                                int source_link_id,
                                const char* source_node_addr,
                                plank_quarantine_reason_t reason,
                                const char* reason_text,
                                const uint8_t* envelope_cbor,
                                size_t envelope_cbor_len);

/* List quarantine */
typedef struct {
    int      id;
    uint8_t  object_id[PLANK_OBJECT_ID_SIZE];
    plank_object_class_t object_class;
    int      source_link_id;
    char     source_node_addr[PLANK_MAX_ADDRESS];
    plank_quarantine_reason_t reason;
    char     reason_text[256];
    char     quarantined_at[32];
    char     reviewed_at[32];
    char     reviewed_by[64];
    int      resolution;
} plank_quarantine_entry_t;

int plank_store_quarantine_list(plank_store_t* store, plank_quarantine_entry_t* out, int max);

/* Release from quarantine */
bool plank_store_quarantine_release(plank_store_t* store, int id, const char* reviewed_by);

/* Reject from quarantine */
bool plank_store_quarantine_reject(plank_store_t* store, int id, const char* reviewed_by);

/* ============================================================================
 * USER PACKET EXPORTS
 * ============================================================================ */

typedef struct {
    int      id;
    uint8_t  export_id[PLANK_EXPORT_ID_SIZE];
    int      user_id;
    uint8_t  bundle_id[PLANK_BUNDLE_ID_SIZE];
    char     created_at[32];
    char     exported_at[32];
    char     packet_path[512];
    int      message_count;
    int      attachment_count;
    uint64_t cursor_low;
    uint64_t cursor_high;
    int      status;
} plank_user_export_t;

/* Create user export record */
bool plank_store_user_export_create(plank_store_t* store, int user_id,
                                    const uint8_t* export_id, int* id_out);

/* Update user export after packet creation */
bool plank_store_user_export_complete(plank_store_t* store, int id,
                                      const uint8_t* bundle_id,
                                      const char* packet_path,
                                      int message_count,
                                      int attachment_count,
                                      uint64_t cursor_low,
                                      uint64_t cursor_high);

/* Get user export by export ID */
bool plank_store_user_export_get(plank_store_t* store, const uint8_t* export_id,
                                 plank_user_export_t* out);

/* List user exports */
int plank_store_user_export_list(plank_store_t* store, int user_id,
                                 plank_user_export_t* out, int max);

/* ============================================================================
 * OUTBOUND QUEUE
 * ============================================================================ */

/* Queue object for outbound to link */
bool plank_store_outbound_queue(plank_store_t* store, int link_id,
                                const uint8_t* object_id, int priority);

/* Get pending outbound for link */
int plank_store_outbound_pending(plank_store_t* store, int link_id,
                                 uint8_t** object_ids_out, int max);

/* Mark outbound as bundled */
bool plank_store_outbound_mark_bundled(plank_store_t* store, int link_id,
                                       const uint8_t* object_id,
                                       const uint8_t* bundle_id);

/* Mark outbound as sent */
bool plank_store_outbound_mark_sent(plank_store_t* store, int link_id,
                                    const uint8_t* bundle_id);

/* Mark outbound as acked */
bool plank_store_outbound_mark_acked(plank_store_t* store, int link_id,
                                     const uint8_t* bundle_id);

/* ============================================================================
 * RECEIPTS
 * ============================================================================ */

/* Store receipt */
bool plank_store_receipt_add(plank_store_t* store,
                             const uint8_t* target_id,
                             plank_target_kind_t target_kind,
                             plank_receipt_type_t receipt_type,
                             int link_id,
                             int accepted_count,
                             int duplicate_count,
                             int rejected_count,
                             int quarantine_count,
                             const char* details);

/* ============================================================================
 * AUDIT LOG
 * ============================================================================ */

/* Log audit event */
bool plank_store_audit_log(plank_store_t* store,
                           const char* event_type,
                           int link_id,
                           const char* node_addr,
                           const char* user_handle,
                           const uint8_t* object_id,
                           const char* details);

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* Get config value */
bool plank_store_config_get(plank_store_t* store, const char* key, char* value_out, size_t max);

/* Get config value as int */
int plank_store_config_get_int(plank_store_t* store, const char* key, int default_val);

/* Set config value */
bool plank_store_config_set(plank_store_t* store, const char* key, const char* value);

/* ============================================================================
 * TRANSACTIONS
 * ============================================================================ */

/* Begin transaction */
bool plank_store_begin(plank_store_t* store);

/* Commit transaction */
bool plank_store_commit(plank_store_t* store);

/* Rollback transaction */
bool plank_store_rollback(plank_store_t* store);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_STORE_H */
