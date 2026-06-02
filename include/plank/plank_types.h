/*
 * PLANK Type Definitions
 * Packet Link for Area Networked Knowledge
 *
 * Core types, enums, and constants for the PLANK protocol.
 */

#ifndef PLANK_TYPES_H
#define PLANK_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SIZE CONSTANTS
 * ============================================================================ */

#define PLANK_NODE_ID_SIZE      16
#define PLANK_LINK_ID_SIZE      16
#define PLANK_OBJECT_ID_SIZE    32
#define PLANK_ATTACHMENT_ID_SIZE 32
#define PLANK_BUNDLE_ID_SIZE    32
#define PLANK_EXPORT_ID_SIZE    16
#define PLANK_NONCE_SIZE        32
#define PLANK_SIGNATURE_SIZE    64
#define PLANK_PUBKEY_SIZE       32
#define PLANK_PRIVKEY_SIZE      64
#define PLANK_HASH_SIZE         32

#define PLANK_MAX_NODE_NAME     32
#define PLANK_MAX_NETWORK_NAME  32
#define PLANK_MAX_USER_NAME     32
#define PLANK_MAX_AREA_SLUG     48
#define PLANK_MAX_SUBJECT       256
#define PLANK_MAX_FILENAME      256
#define PLANK_MAX_MIME_TYPE     128
#define PLANK_MAX_ADDRESS       128
#define PLANK_MAX_REASON_TEXT   512

#define PLANK_FRAME_HEADER_SIZE 24
#define PLANK_BUNDLE_HEADER_SIZE 32
#define PLANK_DIRENT_SIZE       88

#define PLANK_DEFAULT_MAX_FRAME_SIZE   65536
#define PLANK_DEFAULT_MAX_BUNDLE_SIZE  16777216
#define PLANK_DEFAULT_PORT             2930

/* ============================================================================
 * PROTOCOL VERSION
 * ============================================================================ */

#define PLANK_PROTOCOL_VERSION  1
#define PLANK_BUNDLE_FORMAT_VERSION 1

/* ============================================================================
 * MAGIC VALUES
 * ============================================================================ */

#define PLANK_FRAME_MAGIC       "PLK1"
#define PLANK_BUNDLE_MAGIC      "PLKB"
#define PLANK_BUNDLE_TERMINAL   "PLEND1\0\0"

/* ============================================================================
 * OBJECT CLASS ENUM
 * ============================================================================ */

typedef enum {
    PLANK_CLASS_AREA_DEFINITION   = 0x0001,
    PLANK_CLASS_AREA_POLICY       = 0x0002,
    PLANK_CLASS_MESSAGE           = 0x0003,
    PLANK_CLASS_ATTACHMENT_META   = 0x0004,
    PLANK_CLASS_SUBSCRIPTION      = 0x0005,
    PLANK_CLASS_MODERATION        = 0x0006,
    PLANK_CLASS_ROUTING           = 0x0007,
    PLANK_CLASS_RECEIPT           = 0x0008,
    PLANK_CLASS_LINK_EVENT        = 0x0009,
    PLANK_CLASS_BUNDLE_CHECKPOINT = 0x000A,
    PLANK_CLASS_NODE_INFO         = 0x000B
} plank_object_class_t;

/* ============================================================================
 * MESSAGE TYPE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_MSG_AREA_POST   = 0x0001,
    PLANK_MSG_DIRECT_POST = 0x0002,
    PLANK_MSG_SYSTEM_POST = 0x0003
} plank_message_type_t;

/* ============================================================================
 * BODY FORMAT ENUM
 * ============================================================================ */

typedef enum {
    PLANK_BODY_PLAIN_UTF8    = 0x0001,
    PLANK_BODY_MUTINEER_ANSI = 0x0002,
    PLANK_BODY_MARKDOWN_SAFE = 0x0003
} plank_body_format_t;

/* ============================================================================
 * VISIBILITY ENUM
 * ============================================================================ */

typedef enum {
    PLANK_VIS_VISIBLE        = 0x0001,
    PLANK_VIS_LOCAL_ONLY     = 0x0002,
    PLANK_VIS_MODERATED_HOLD = 0x0003,
    PLANK_VIS_HIDDEN         = 0x0004
} plank_visibility_t;

/* ============================================================================
 * MESSAGE FLAGS
 * ============================================================================ */

#define PLANK_MSG_FLAG_NO_REPLY           (1u << 0)
#define PLANK_MSG_FLAG_SYSOP_ONLY         (1u << 1)
#define PLANK_MSG_FLAG_PINNED             (1u << 2)
#define PLANK_MSG_FLAG_LOCKED             (1u << 3)
#define PLANK_MSG_FLAG_PRIVATE            (1u << 4)
#define PLANK_MSG_FLAG_MODERATED          (1u << 5)
#define PLANK_MSG_FLAG_ATTACHMENTS        (1u << 6)

/* ============================================================================
 * RETENTION CLASS ENUM
 * ============================================================================ */

typedef enum {
    PLANK_RETENTION_EPHEMERAL  = 0x0001,
    PLANK_RETENTION_STANDARD   = 0x0002,
    PLANK_RETENTION_LONG_TERM  = 0x0003,
    PLANK_RETENTION_ARCHIVE    = 0x0004,
    PLANK_RETENTION_LEGAL_HOLD = 0x0005
} plank_retention_class_t;

/* ============================================================================
 * COMPRESSION ENUM
 * ============================================================================ */

typedef enum {
    PLANK_COMP_NONE = 0x0000,
    PLANK_COMP_ZSTD = 0x0001
} plank_compression_t;

/* ============================================================================
 * DISTRIBUTION MODE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_DIST_FANOUT            = 0x0001,
    PLANK_DIST_SUBSCRIPTION_ONLY = 0x0002,
    PLANK_DIST_RESTRICTED        = 0x0003,
    PLANK_DIST_LOCAL_MIRROR      = 0x0004
} plank_distribution_mode_t;

/* ============================================================================
 * MODERATION MODE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_MOD_OPEN           = 0x0001,
    PLANK_MOD_PREMOD         = 0x0002,
    PLANK_MOD_POSTMOD        = 0x0003,
    PLANK_MOD_SYSOP_ONLY     = 0x0004,
    PLANK_MOD_READ_ONLY      = 0x0005,
    PLANK_MOD_PRIVATE_INVITE = 0x0006
} plank_moderation_mode_t;

/* ============================================================================
 * SUBSCRIPTION ACTION ENUM
 * ============================================================================ */

typedef enum {
    PLANK_SUB_SUBSCRIBE   = 0x0001,
    PLANK_SUB_UNSUBSCRIBE = 0x0002,
    PLANK_SUB_PAUSE       = 0x0003,
    PLANK_SUB_RESUME      = 0x0004
} plank_subscription_action_t;

/* ============================================================================
 * MODERATION ACTION ENUM
 * ============================================================================ */

typedef enum {
    PLANK_MODACT_APPROVE       = 0x0001,
    PLANK_MODACT_REJECT        = 0x0002,
    PLANK_MODACT_HIDE          = 0x0003,
    PLANK_MODACT_UNHIDE        = 0x0004,
    PLANK_MODACT_LOCK_THREAD   = 0x0005,
    PLANK_MODACT_UNLOCK_THREAD = 0x0006,
    PLANK_MODACT_PIN           = 0x0007,
    PLANK_MODACT_UNPIN         = 0x0008,
    PLANK_MODACT_TOMBSTONE     = 0x0009,
    PLANK_MODACT_RESTORE       = 0x000A
} plank_moderation_action_t;

/* ============================================================================
 * RECEIPT TYPE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_RCPT_BUNDLE_ACCEPTED  = 0x0001,
    PLANK_RCPT_BUNDLE_DUPLICATE = 0x0002,
    PLANK_RCPT_BUNDLE_PARTIAL   = 0x0003,
    PLANK_RCPT_BUNDLE_REJECTED  = 0x0004,
    PLANK_RCPT_OBJECT_STORED    = 0x0005,
    PLANK_RCPT_OBJECT_VERIFIED  = 0x0006,
    PLANK_RCPT_OBJECT_REJECTED  = 0x0007,
    PLANK_RCPT_AREA_IMPORTED    = 0x0008,
    PLANK_RCPT_LOCAL_DELIVERED  = 0x0009,
    PLANK_RCPT_ROUTED_ONWARD    = 0x000A,
    PLANK_RCPT_FINALIZED        = 0x000B
} plank_receipt_type_t;

/* ============================================================================
 * SIGNATURE ALGORITHM ENUM
 * ============================================================================ */

typedef enum {
    PLANK_SIG_ED25519 = 0x0001
} plank_sig_alg_t;

/* ============================================================================
 * FRAME TYPE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_FRAME_HELLO          = 0x0001,
    PLANK_FRAME_HELLO_ACK      = 0x0002,
    PLANK_FRAME_AUTH_PROOF     = 0x0003,
    PLANK_FRAME_CAPS           = 0x0004,
    PLANK_FRAME_BUNDLE_OFFER   = 0x0005,
    PLANK_FRAME_BUNDLE_REQUEST = 0x0006,
    PLANK_FRAME_BUNDLE_DATA    = 0x0007,
    PLANK_FRAME_RECEIPT        = 0x0008,
    PLANK_FRAME_PING           = 0x0009,
    PLANK_FRAME_PONG           = 0x000A,
    PLANK_FRAME_ERROR          = 0x000B,
    PLANK_FRAME_CLOSE          = 0x000C
} plank_frame_type_t;

/* ============================================================================
 * FRAME FLAGS
 * ============================================================================ */

#define PLANK_FLAG_MORE       (1u << 0)
#define PLANK_FLAG_ACK_REQ    (1u << 1)
#define PLANK_FLAG_COMPRESSED (1u << 2)
#define PLANK_FLAG_FINAL      (1u << 3)

/* ============================================================================
 * CAPABILITY BITS
 * ============================================================================ */

#define PLANK_CAP_BUNDLES_ZSTD        (1ULL << 0)
#define PLANK_CAP_OBJECT_SIGS         (1ULL << 1)
#define PLANK_CAP_UTF8_TEXT           (1ULL << 2)
#define PLANK_CAP_ATTACHMENTS         (1ULL << 3)
#define PLANK_CAP_OFFLINE_USER_PKTS   (1ULL << 4)
#define PLANK_CAP_RECEIPTS            (1ULL << 5)
#define PLANK_CAP_CURSOR_SYNC         (1ULL << 6)
#define PLANK_CAP_INLINE_ATTACHMENTS  (1ULL << 16)
#define PLANK_CAP_STREAM_ATTACHMENTS  (1ULL << 17)
#define PLANK_CAP_BODY_MARKDOWN_SAFE  (1ULL << 18)
#define PLANK_CAP_BODY_MUTINEER_ANSI  (1ULL << 19)
#define PLANK_CAP_KEY_ROLLOVER        (1ULL << 20)
#define PLANK_CAP_DELTA_REQUESTS      (1ULL << 21)

#define PLANK_CAP_REQUIRED (PLANK_CAP_BUNDLES_ZSTD | PLANK_CAP_OBJECT_SIGS | \
                            PLANK_CAP_UTF8_TEXT | PLANK_CAP_RECEIPTS | \
                            PLANK_CAP_CURSOR_SYNC)

/* ============================================================================
 * BUNDLE TYPE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_BUNDLE_LINK_SYNC     = 0x0001,
    PLANK_BUNDLE_USER_EXPORT   = 0x0002,
    PLANK_BUNDLE_USER_REPLY    = 0x0003,
    PLANK_BUNDLE_ADMIN_TRANSFER = 0x0004
} plank_bundle_type_t;

/* ============================================================================
 * RECORD TYPE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_RECORD_OBJECT     = 0x0001,
    PLANK_RECORD_ATTACHMENT = 0x0002,
    PLANK_RECORD_CHECKPOINT = 0x0003,
    PLANK_RECORD_INDEX      = 0x0004,
    PLANK_RECORD_NOTE       = 0x0005
} plank_record_type_t;

/* ============================================================================
 * LINK STATE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_LINK_DISABLED   = 0,
    PLANK_LINK_IDLE       = 1,
    PLANK_LINK_CONNECTING = 2,
    PLANK_LINK_TLS_OK     = 3,
    PLANK_LINK_AUTH_OK    = 4,
    PLANK_LINK_SYNCING    = 5,
    PLANK_LINK_BACKOFF    = 6,
    PLANK_LINK_FAILED     = 7
} plank_link_state_t;

/* ============================================================================
 * LINK DIRECTION
 * ============================================================================ */

typedef enum {
    PLANK_DIR_INBOUND  = 1,
    PLANK_DIR_OUTBOUND = 2,
    PLANK_DIR_BOTH     = 3
} plank_link_direction_t;

/* ============================================================================
 * ERROR CODES
 * ============================================================================ */

typedef enum {
    PLANK_ERR_NONE              = 0x0000,
    PLANK_ERR_PROTOCOL_VERSION  = 0x0001,
    PLANK_ERR_AUTH_FAILED       = 0x0002,
    PLANK_ERR_UNKNOWN_LINK      = 0x0003,
    PLANK_ERR_CAPS_MISMATCH     = 0x0004,
    PLANK_ERR_BUNDLE_TOO_LARGE  = 0x0005,
    PLANK_ERR_BUNDLE_CORRUPT    = 0x0006,
    PLANK_ERR_OBJECT_REJECTED   = 0x0007,
    PLANK_ERR_DUPLICATE         = 0x0008,
    PLANK_ERR_AREA_UNKNOWN      = 0x0009,
    PLANK_ERR_SUBSCRIPTION_DENIED = 0x000A,
    PLANK_ERR_RATE_LIMIT        = 0x000B,
    PLANK_ERR_INTERNAL          = 0x000C
} plank_error_code_t;

/* ============================================================================
 * RECEIPT CODE ENUM
 * ============================================================================ */

typedef enum {
    PLANK_RC_OK          = 0x0001,
    PLANK_RC_DUPLICATE   = 0x0002,
    PLANK_RC_PARTIAL     = 0x0003,
    PLANK_RC_REJECTED    = 0x0004,
    PLANK_RC_QUARANTINED = 0x0005
} plank_receipt_code_t;

/* ============================================================================
 * TARGET KIND ENUM
 * ============================================================================ */

typedef enum {
    PLANK_TARGET_BUNDLE = 0x0001,
    PLANK_TARGET_OBJECT = 0x0002,
    PLANK_TARGET_LINK   = 0x0003
} plank_target_kind_t;

/* ============================================================================
 * SOURCE KIND ENUM
 * ============================================================================ */

typedef enum {
    PLANK_SOURCE_LOCAL   = 1,
    PLANK_SOURCE_LINK    = 2,
    PLANK_SOURCE_IMPORT  = 3,
    PLANK_SOURCE_OFFLINE = 4
} plank_source_kind_t;

/* ============================================================================
 * QUARANTINE REASON
 * ============================================================================ */

typedef enum {
    PLANK_QUARANTINE_NONE           = 0,
    PLANK_QUARANTINE_BAD_SIGNATURE  = 1,
    PLANK_QUARANTINE_POLICY_DENY    = 2,
    PLANK_QUARANTINE_SIZE_EXCEEDED  = 3,
    PLANK_QUARANTINE_HOP_EXCEEDED   = 4,
    PLANK_QUARANTINE_LOOP_DETECTED  = 5,
    PLANK_QUARANTINE_BANNED_NODE    = 6,
    PLANK_QUARANTINE_BANNED_USER    = 7,
    PLANK_QUARANTINE_AREA_UNKNOWN   = 8,
    PLANK_QUARANTINE_FORMAT_INVALID = 9,
    PLANK_QUARANTINE_MANUAL         = 10
} plank_quarantine_reason_t;

/* ============================================================================
 * IMPORT RESULT
 * ============================================================================ */

typedef enum {
    PLANK_IMPORT_ACCEPTED   = 1,
    PLANK_IMPORT_DUPLICATE  = 2,
    PLANK_IMPORT_PARTIAL    = 3,
    PLANK_IMPORT_REJECTED   = 4,
    PLANK_IMPORT_QUARANTINED = 5
} plank_import_result_t;

#ifdef __cplusplus
}
#endif

#endif /* PLANK_TYPES_H */
