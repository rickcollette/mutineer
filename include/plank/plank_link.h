/*
 * PLANK Link Protocol
 * Packet Link for Area Networked Knowledge
 *
 * Node-to-node session management, framing, and exchange.
 */

#ifndef PLANK_LINK_H
#define PLANK_LINK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"
#include "plank_wire.h"
#include "plank_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * LINK SESSION
 * ============================================================================ */

typedef struct plank_link_session plank_link_session_t;

/* Session callbacks */
typedef struct {
    /* Called when session state changes */
    void (*on_state_change)(plank_link_session_t* session,
                            plank_link_state_t old_state,
                            plank_link_state_t new_state,
                            void* ctx);
    
    /* Called when bundle offer received */
    void (*on_bundle_offer)(plank_link_session_t* session,
                            const uint8_t* offer_id,
                            const uint8_t** bundle_ids,
                            const uint16_t* bundle_types,
                            const uint32_t* object_counts,
                            const uint32_t* sizes,
                            int count,
                            void* ctx);
    
    /* Called when bundle received */
    void (*on_bundle_received)(plank_link_session_t* session,
                               const uint8_t* bundle_id,
                               const uint8_t* data, size_t len,
                               void* ctx);
    
    /* Called when receipt received */
    void (*on_receipt)(plank_link_session_t* session,
                       plank_receipt_code_t code,
                       const uint8_t* target_id,
                       int accepted, int duplicate, int rejected, int quarantine,
                       void* ctx);
    
    /* Called on error */
    void (*on_error)(plank_link_session_t* session,
                     plank_error_code_t code,
                     bool fatal,
                     const char* text,
                     void* ctx);
    
    void* ctx;
} plank_link_callbacks_t;

/* ============================================================================
 * SESSION LIFECYCLE
 * ============================================================================ */

/* Create outbound session */
plank_link_session_t* plank_link_session_create_outbound(
    plank_store_t* store,
    int link_id,
    const plank_link_callbacks_t* callbacks
);

/* Create inbound session (from accepted connection) */
plank_link_session_t* plank_link_session_create_inbound(
    plank_store_t* store,
    int fd,
    const plank_link_callbacks_t* callbacks
);

/* Free session */
void plank_link_session_free(plank_link_session_t* session);

/* Get session state */
plank_link_state_t plank_link_session_state(plank_link_session_t* session);

/* Get link ID */
int plank_link_session_link_id(plank_link_session_t* session);

/* Get file descriptor */
int plank_link_session_fd(plank_link_session_t* session);

/* ============================================================================
 * CONNECTION
 * ============================================================================ */

/* Connect to remote peer (blocking) */
bool plank_link_session_connect(plank_link_session_t* session,
                                const char* host, int port,
                                int timeout_sec);

/* Perform TLS handshake */
bool plank_link_session_tls_handshake(plank_link_session_t* session);

/* Get peer TLS certificate fingerprint */
bool plank_link_session_get_tls_fingerprint(plank_link_session_t* session,
                                            char* fingerprint_out);

/* ============================================================================
 * PROTOCOL HANDSHAKE
 * ============================================================================ */

/* Send HELLO frame */
bool plank_link_session_send_hello(plank_link_session_t* session);

/* Process received HELLO, send HELLO_ACK */
bool plank_link_session_handle_hello(plank_link_session_t* session,
                                     const uint8_t* payload, size_t len);

/* Send AUTH_PROOF frame */
bool plank_link_session_send_auth_proof(plank_link_session_t* session);

/* Verify received AUTH_PROOF */
bool plank_link_session_verify_auth_proof(plank_link_session_t* session,
                                          const uint8_t* payload, size_t len);

/* Send CAPS frame */
bool plank_link_session_send_caps(plank_link_session_t* session);

/* Process received CAPS */
bool plank_link_session_handle_caps(plank_link_session_t* session,
                                    const uint8_t* payload, size_t len);

/* ============================================================================
 * BUNDLE EXCHANGE
 * ============================================================================ */

/* Send bundle offer */
bool plank_link_session_send_bundle_offer(plank_link_session_t* session,
                                          const uint8_t** bundle_ids,
                                          const uint16_t* bundle_types,
                                          const uint32_t* object_counts,
                                          const uint32_t* sizes,
                                          int count,
                                          uint64_t cursor_low,
                                          uint64_t cursor_high);

/* Send bundle request */
bool plank_link_session_send_bundle_request(plank_link_session_t* session,
                                            const uint8_t* offer_id,
                                            const uint8_t** bundle_ids,
                                            int count,
                                            int max_items);

/* Send bundle data */
bool plank_link_session_send_bundle(plank_link_session_t* session,
                                    const uint8_t* bundle_id,
                                    const uint8_t* data, size_t len);

/* Send bundle from file */
bool plank_link_session_send_bundle_file(plank_link_session_t* session,
                                         const uint8_t* bundle_id,
                                         const char* path);

/* Send receipt */
bool plank_link_session_send_receipt(plank_link_session_t* session,
                                     plank_receipt_code_t code,
                                     plank_target_kind_t target_kind,
                                     const uint8_t* target_id,
                                     int accepted, int duplicate,
                                     int rejected, int quarantine,
                                     const char* details);

/* ============================================================================
 * KEEPALIVE
 * ============================================================================ */

/* Send PING */
bool plank_link_session_send_ping(plank_link_session_t* session);

/* Handle PING, send PONG */
bool plank_link_session_handle_ping(plank_link_session_t* session,
                                    const uint8_t* payload, size_t len);

/* ============================================================================
 * ERROR AND CLOSE
 * ============================================================================ */

/* Send ERROR frame */
bool plank_link_session_send_error(plank_link_session_t* session,
                                   plank_error_code_t code,
                                   bool fatal,
                                   const char* text);

/* Send CLOSE frame */
bool plank_link_session_send_close(plank_link_session_t* session,
                                   int reason_code,
                                   const char* text);

/* Close session */
void plank_link_session_close(plank_link_session_t* session);

/* ============================================================================
 * I/O
 * ============================================================================ */

/* Read and process one frame (blocking) */
bool plank_link_session_read_frame(plank_link_session_t* session);

/* Process all available frames (non-blocking) */
int plank_link_session_process(plank_link_session_t* session);

/* Get last error */
const char* plank_link_session_error(plank_link_session_t* session);

/* ============================================================================
 * FRAME PARSING
 * ============================================================================ */

/* Parse frame header */
bool plank_frame_parse_header(const uint8_t* data, size_t len,
                              plank_frame_hdr_t* hdr_out);

/* Validate frame header */
bool plank_frame_validate_header(const plank_frame_hdr_t* hdr,
                                 uint32_t max_payload_len);

/* ============================================================================
 * FRAME PAYLOAD ENCODING
 * ============================================================================ */

/* Encode HELLO payload */
bool plank_frame_encode_hello(
    uint8_t** payload_out, size_t* len_out,
    uint16_t protocol_version,
    const uint8_t* node_id,
    const char* node_addr,
    const uint8_t* link_id,
    const uint8_t* session_nonce,
    const char* software_name,
    const char* software_version,
    uint64_t timestamp,
    uint32_t max_frame_size,
    uint32_t max_bundle_size,
    uint64_t cap_bitmap
);

/* Encode HELLO_ACK payload */
bool plank_frame_encode_hello_ack(
    uint8_t** payload_out, size_t* len_out,
    uint16_t protocol_version,
    const uint8_t* node_id,
    const char* node_addr,
    const uint8_t* link_id,
    const uint8_t* session_nonce,
    uint32_t accepted_max_frame_size,
    uint32_t accepted_max_bundle_size,
    uint64_t cap_bitmap
);

/* Encode AUTH_PROOF payload */
bool plank_frame_encode_auth_proof(
    uint8_t** payload_out, size_t* len_out,
    const uint8_t* signing_node_id,
    const uint8_t* link_id,
    plank_sig_alg_t sig_alg,
    const uint8_t* signature,
    const uint8_t* transcript_hash
);

/* Encode CAPS payload */
bool plank_frame_encode_caps(
    uint8_t** payload_out, size_t* len_out,
    uint64_t cap_bitmap,
    uint32_t max_frame_size,
    uint32_t max_bundle_size,
    const uint16_t* compressions, int comp_count,
    const uint16_t* body_formats, int format_count
);

/* Encode BUNDLE_OFFER payload */
bool plank_frame_encode_bundle_offer(
    uint8_t** payload_out, size_t* len_out,
    const uint8_t* offer_id,
    const uint8_t** bundle_ids, int count,
    const uint16_t* bundle_types,
    const uint32_t* object_counts,
    const uint32_t* encoded_sizes,
    uint64_t cursor_low,
    uint64_t cursor_high,
    const char* notes
);

/* Encode BUNDLE_REQUEST payload */
bool plank_frame_encode_bundle_request(
    uint8_t** payload_out, size_t* len_out,
    const uint8_t* offer_id,
    const uint8_t** bundle_ids, int count,
    uint32_t max_items,
    const char* notes
);

/* Encode RECEIPT payload */
bool plank_frame_encode_receipt(
    uint8_t** payload_out, size_t* len_out,
    plank_receipt_code_t code,
    plank_target_kind_t target_kind,
    const uint8_t* target_id,
    int accepted, int duplicate, int rejected, int quarantine,
    const char* details
);

/* Encode ERROR payload */
bool plank_frame_encode_error(
    uint8_t** payload_out, size_t* len_out,
    plank_error_code_t code,
    bool fatal,
    const char* text,
    const uint8_t* target_id
);

/* Encode PING/PONG payload */
bool plank_frame_encode_ping(
    uint8_t** payload_out, size_t* len_out,
    uint64_t timestamp,
    const uint8_t* nonce
);

/* Encode CLOSE payload */
bool plank_frame_encode_close(
    uint8_t** payload_out, size_t* len_out,
    int reason_code,
    const char* text
);

/* Free encoded payload */
void plank_frame_free_payload(uint8_t* payload);

/* ============================================================================
 * FRAME PAYLOAD DECODING
 * ============================================================================ */

typedef struct {
    uint16_t protocol_version;
    uint8_t  node_id[PLANK_NODE_ID_SIZE];
    char     node_addr[PLANK_MAX_ADDRESS];
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    uint8_t  session_nonce[PLANK_NONCE_SIZE];
    char     software_name[64];
    char     software_version[32];
    uint64_t timestamp;
    uint32_t max_frame_size;
    uint32_t max_bundle_size;
    uint64_t cap_bitmap;
} plank_hello_payload_t;

bool plank_frame_decode_hello(const uint8_t* payload, size_t len,
                              plank_hello_payload_t* out);

typedef struct {
    uint16_t protocol_version;
    uint8_t  node_id[PLANK_NODE_ID_SIZE];
    char     node_addr[PLANK_MAX_ADDRESS];
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    uint8_t  session_nonce[PLANK_NONCE_SIZE];
    uint32_t accepted_max_frame_size;
    uint32_t accepted_max_bundle_size;
    uint64_t cap_bitmap;
} plank_hello_ack_payload_t;

bool plank_frame_decode_hello_ack(const uint8_t* payload, size_t len,
                                  plank_hello_ack_payload_t* out);

typedef struct {
    uint8_t  signing_node_id[PLANK_NODE_ID_SIZE];
    uint8_t  link_id[PLANK_LINK_ID_SIZE];
    plank_sig_alg_t sig_alg;
    uint8_t  signature[PLANK_SIGNATURE_SIZE];
    uint8_t  transcript_hash[PLANK_HASH_SIZE];
} plank_auth_proof_payload_t;

bool plank_frame_decode_auth_proof(const uint8_t* payload, size_t len,
                                   plank_auth_proof_payload_t* out);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_LINK_H */
