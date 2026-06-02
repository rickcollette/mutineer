/*
 * PLANK Bundle Operations
 * Packet Link for Area Networked Knowledge
 *
 * Creation, parsing, verification, and extraction of .plb and .plp files.
 */

#ifndef PLANK_BUNDLE_H
#define PLANK_BUNDLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include "plank_types.h"
#include "plank_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * BUNDLE MANIFEST
 * ============================================================================ */

typedef struct {
    uint8_t  bundle_id[PLANK_BUNDLE_ID_SIZE];
    plank_bundle_type_t bundle_type;
    uint8_t  source_node_id[PLANK_NODE_ID_SIZE];
    char     source_node_addr[PLANK_MAX_ADDRESS];
    uint8_t  target_node_id[PLANK_NODE_ID_SIZE];    /* for LINK_SYNC */
    uint8_t  export_id[PLANK_EXPORT_ID_SIZE];       /* for USER_EXPORT/REPLY */
    uint64_t created_at;
    uint32_t record_count;
    uint32_t object_count;
    uint32_t attachment_count;
    plank_compression_t compression_mode;
    uint8_t  record_directory_hash[PLANK_HASH_SIZE];
    char     scope[256];                            /* area filter or description */
    uint64_t cursor_low;
    uint64_t cursor_high;
    char     notes[512];
    plank_sig_alg_t sig_alg;
    uint8_t  signature[PLANK_SIGNATURE_SIZE];
} plank_bundle_manifest_t;

/* ============================================================================
 * BUNDLE RECORD
 * ============================================================================ */

typedef struct {
    plank_record_type_t record_type;
    uint16_t flags;
    uint64_t offset;
    uint64_t encoded_len;
    uint64_t decoded_len;
    uint8_t  digest[PLANK_HASH_SIZE];
    uint8_t  record_id[24];
    
    /* Payload (loaded on demand) */
    uint8_t* payload;
    size_t   payload_len;
} plank_bundle_record_t;

/* ============================================================================
 * BUNDLE WRITER
 * ============================================================================ */

typedef struct plank_bundle_writer plank_bundle_writer_t;

/* Create bundle writer for file */
plank_bundle_writer_t* plank_bundle_writer_create(
    const char* path,
    plank_bundle_type_t type,
    const uint8_t* source_node_id,
    const char* source_node_addr
);

/* Set target node (for LINK_SYNC) */
void plank_bundle_writer_set_target(plank_bundle_writer_t* w,
                                    const uint8_t* target_node_id);

/* Set export ID (for USER_EXPORT/REPLY) */
void plank_bundle_writer_set_export_id(plank_bundle_writer_t* w,
                                       const uint8_t* export_id);

/* Set cursor range */
void plank_bundle_writer_set_cursors(plank_bundle_writer_t* w,
                                     uint64_t cursor_low, uint64_t cursor_high);

/* Set scope description */
void plank_bundle_writer_set_scope(plank_bundle_writer_t* w, const char* scope);

/* Set notes */
void plank_bundle_writer_set_notes(plank_bundle_writer_t* w, const char* notes);

/* Set compression mode */
void plank_bundle_writer_set_compression(plank_bundle_writer_t* w,
                                         plank_compression_t mode);

/* Add object record */
bool plank_bundle_writer_add_object(plank_bundle_writer_t* w,
                                    const plank_object_t* obj);

/* Add attachment record */
bool plank_bundle_writer_add_attachment(plank_bundle_writer_t* w,
                                        const uint8_t* attachment_id,
                                        const uint8_t* data, size_t len,
                                        bool compress);

/* Add attachment from file */
bool plank_bundle_writer_add_attachment_file(plank_bundle_writer_t* w,
                                             const uint8_t* attachment_id,
                                             const char* path,
                                             bool compress);

/* Add checkpoint record */
bool plank_bundle_writer_add_checkpoint(plank_bundle_writer_t* w,
                                        const uint8_t* link_id,
                                        uint16_t direction,
                                        uint64_t seq_low,
                                        uint64_t seq_high);

/* Add index record */
bool plank_bundle_writer_add_index(plank_bundle_writer_t* w,
                                   const uint8_t* data, size_t len);

/* Add note record */
bool plank_bundle_writer_add_note(plank_bundle_writer_t* w,
                                  const char* note);

/* Finalize and sign bundle */
bool plank_bundle_writer_finalize(plank_bundle_writer_t* w,
                                  const uint8_t* signing_key_priv);

/* Get bundle ID after finalization */
bool plank_bundle_writer_get_id(plank_bundle_writer_t* w, uint8_t* bundle_id_out);

/* Close writer and free resources */
void plank_bundle_writer_close(plank_bundle_writer_t* w);

/* Get last error message */
const char* plank_bundle_writer_error(plank_bundle_writer_t* w);

/* ============================================================================
 * BUNDLE READER
 * ============================================================================ */

typedef struct plank_bundle_reader plank_bundle_reader_t;

/* Open bundle for reading */
plank_bundle_reader_t* plank_bundle_reader_open(const char* path);

/* Open bundle from memory */
plank_bundle_reader_t* plank_bundle_reader_open_mem(const uint8_t* data, size_t len);

/* Get manifest */
const plank_bundle_manifest_t* plank_bundle_reader_manifest(plank_bundle_reader_t* r);

/* Verify bundle signature */
bool plank_bundle_reader_verify(plank_bundle_reader_t* r, const uint8_t* signing_key_pub);

/* Verify bundle integrity (all digests) */
bool plank_bundle_reader_verify_integrity(plank_bundle_reader_t* r);

/* Get record count */
uint32_t plank_bundle_reader_record_count(plank_bundle_reader_t* r);

/* Get record by index */
bool plank_bundle_reader_get_record(plank_bundle_reader_t* r, uint32_t index,
                                    plank_bundle_record_t* out);

/* Load record payload */
bool plank_bundle_reader_load_payload(plank_bundle_reader_t* r, uint32_t index,
                                      uint8_t** data_out, size_t* len_out);

/* Free loaded payload */
void plank_bundle_reader_free_payload(uint8_t* data);

/* Iterate objects */
typedef bool (*plank_bundle_object_cb)(const plank_object_t* obj, void* ctx);
bool plank_bundle_reader_foreach_object(plank_bundle_reader_t* r,
                                        plank_bundle_object_cb cb, void* ctx);

/* Iterate attachments */
typedef bool (*plank_bundle_attachment_cb)(const uint8_t* attachment_id,
                                           const uint8_t* data, size_t len,
                                           void* ctx);
bool plank_bundle_reader_foreach_attachment(plank_bundle_reader_t* r,
                                            plank_bundle_attachment_cb cb, void* ctx);

/* Close reader */
void plank_bundle_reader_close(plank_bundle_reader_t* r);

/* Get last error message */
const char* plank_bundle_reader_error(plank_bundle_reader_t* r);

/* ============================================================================
 * BUNDLE VERIFICATION
 * ============================================================================ */

typedef struct {
    bool     valid;
    bool     signature_ok;
    bool     manifest_ok;
    bool     directory_ok;
    bool     records_ok;
    uint32_t records_verified;
    uint32_t records_failed;
    char     error[256];
} plank_bundle_verify_result_t;

/* Verify bundle file */
bool plank_bundle_verify_file(const char* path, const uint8_t* signing_key_pub,
                              plank_bundle_verify_result_t* result);

/* ============================================================================
 * BUNDLE IMPORT
 * ============================================================================ */

typedef struct plank_store plank_store_t;

typedef struct {
    plank_import_result_t result;
    int      objects_accepted;
    int      objects_duplicate;
    int      objects_rejected;
    int      objects_quarantined;
    int      attachments_stored;
    char     error[256];
} plank_bundle_import_result_t;

/* Import bundle into store */
bool plank_bundle_import(plank_store_t* store, const char* path,
                         int source_link_id, const uint8_t* signing_key_pub,
                         plank_bundle_import_result_t* result);

/* Import bundle from memory */
bool plank_bundle_import_mem(plank_store_t* store, const uint8_t* data, size_t len,
                             int source_link_id, const uint8_t* signing_key_pub,
                             plank_bundle_import_result_t* result);

/* ============================================================================
 * BUNDLE EXPORT
 * ============================================================================ */

/* Export objects for link since cursor */
bool plank_bundle_export_for_link(plank_store_t* store, int link_id,
                                  const char* output_path,
                                  const uint8_t* signing_key_priv,
                                  uint64_t* new_cursor_out,
                                  int* object_count_out);

/* Export user packet */
bool plank_bundle_export_user_packet(plank_store_t* store, int user_id,
                                     const char** area_addrs, int area_count,
                                     bool include_direct_mail,
                                     const char* output_path,
                                     const uint8_t* signing_key_priv,
                                     uint8_t* export_id_out,
                                     int* message_count_out);

/* Export user packet with offline-reader filtering options. */
bool plank_bundle_export_user_packet_ex(plank_store_t* store, int user_id,
                                        const char** area_addrs, int area_count,
                                        bool include_direct_mail,
                                        bool include_read,
                                        int max_messages,
                                        const char* output_path,
                                        const uint8_t* signing_key_priv,
                                        uint8_t* export_id_out,
                                        int* message_count_out);

/* ============================================================================
 * USER REPLY IMPORT
 * ============================================================================ */

typedef struct {
    plank_import_result_t result;
    int      messages_imported;
    int      messages_duplicate;
    int      messages_rejected;
    char     error[256];
} plank_reply_import_result_t;

/* Import user reply packet */
bool plank_bundle_import_reply(plank_store_t* store, const char* path,
                               int user_id, const uint8_t* signing_key_pub,
                               plank_reply_import_result_t* result);

/* ============================================================================
 * BUNDLE INSPECTION
 * ============================================================================ */

/* Print bundle info to file */
void plank_bundle_inspect(const char* path, FILE* out);

/* Get bundle type from file */
plank_bundle_type_t plank_bundle_get_type(const char* path);

/* Get bundle ID from file */
bool plank_bundle_get_id(const char* path, uint8_t* bundle_id_out);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_BUNDLE_H */
