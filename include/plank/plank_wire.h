/*
 * PLANK Wire Protocol Structures
 * Packet Link for Area Networked Knowledge
 *
 * Binary wire format structures for frames, bundles, and directory entries.
 * All multi-byte fields are in network byte order (big-endian).
 */

#ifndef PLANK_WIRE_H
#define PLANK_WIRE_H

#include <stdint.h>
#include <string.h>
#include "plank_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * WIRE FRAME HEADER (24 bytes)
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];          /* "PLK1" */
    uint16_t frame_type;        /* network byte order */
    uint16_t flags;             /* network byte order */
    uint32_t payload_len;       /* network byte order */
    uint64_t correlation_id;    /* network byte order */
    uint32_t reserved;          /* MUST be zero */
} plank_frame_hdr_t;
#pragma pack(pop)

_Static_assert(sizeof(plank_frame_hdr_t) == PLANK_FRAME_HEADER_SIZE,
               "Frame header must be 24 bytes");

/* ============================================================================
 * BUNDLE FILE HEADER (32 bytes)
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];          /* "PLKB" */
    uint16_t format_version;    /* network byte order */
    uint16_t bundle_type;       /* network byte order */
    uint32_t flags;             /* network byte order */
    uint32_t manifest_len;      /* network byte order */
    uint32_t dir_count;         /* network byte order */
    uint16_t dir_entry_size;    /* network byte order, MUST be 88 */
    uint8_t  reserved[10];      /* MUST be zero */
} plank_bundle_hdr_t;
#pragma pack(pop)

_Static_assert(sizeof(plank_bundle_hdr_t) == PLANK_BUNDLE_HEADER_SIZE,
               "Bundle header must be 32 bytes");

/* ============================================================================
 * BUNDLE DIRECTORY ENTRY (88 bytes)
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    uint16_t record_type;       /* network byte order */
    uint16_t flags;             /* network byte order */
    uint32_t reserved;          /* MUST be zero */
    uint64_t offset;            /* network byte order */
    uint64_t encoded_len;       /* network byte order */
    uint64_t decoded_len;       /* network byte order */
    uint8_t  digest[32];        /* SHA-256 of payload bytes as stored */
    uint8_t  record_id[24];     /* abbreviated stable ID */
} plank_bundle_dirent_t;
#pragma pack(pop)

_Static_assert(sizeof(plank_bundle_dirent_t) == PLANK_DIRENT_SIZE,
               "Directory entry must be 88 bytes");

/* ============================================================================
 * BUNDLE TERMINAL MARKER (8 bytes)
 * ============================================================================ */

#pragma pack(push, 1)
typedef struct {
    uint8_t marker[8];          /* "PLEND1\0\0" */
} plank_bundle_terminal_t;
#pragma pack(pop)

/* ============================================================================
 * BYTE ORDER HELPERS
 * ============================================================================ */

static inline uint16_t plank_htons(uint16_t h) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
#else
    return h;
#endif
}

static inline uint16_t plank_ntohs(uint16_t n) {
    return plank_htons(n);
}

static inline uint32_t plank_htonl(uint32_t h) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((h & 0xFF) << 24) | ((h & 0xFF00) << 8) |
           ((h >> 8) & 0xFF00) | ((h >> 24) & 0xFF);
#else
    return h;
#endif
}

static inline uint32_t plank_ntohl(uint32_t n) {
    return plank_htonl(n);
}

static inline uint64_t plank_htonll(uint64_t h) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)plank_htonl((uint32_t)h) << 32) |
           plank_htonl((uint32_t)(h >> 32));
#else
    return h;
#endif
}

static inline uint64_t plank_ntohll(uint64_t n) {
    return plank_htonll(n);
}

/* ============================================================================
 * FRAME HEADER HELPERS
 * ============================================================================ */

static inline void plank_frame_hdr_init(plank_frame_hdr_t* hdr,
                                        plank_frame_type_t type,
                                        uint16_t flags,
                                        uint32_t payload_len,
                                        uint64_t correlation_id) {
    hdr->magic[0] = 'P';
    hdr->magic[1] = 'L';
    hdr->magic[2] = 'K';
    hdr->magic[3] = '1';
    hdr->frame_type = plank_htons((uint16_t)type);
    hdr->flags = plank_htons(flags);
    hdr->payload_len = plank_htonl(payload_len);
    hdr->correlation_id = plank_htonll(correlation_id);
    hdr->reserved = 0;
}

static inline bool plank_frame_hdr_valid_magic(const plank_frame_hdr_t* hdr) {
    return hdr->magic[0] == 'P' && hdr->magic[1] == 'L' &&
           hdr->magic[2] == 'K' && hdr->magic[3] == '1';
}

static inline plank_frame_type_t plank_frame_hdr_type(const plank_frame_hdr_t* hdr) {
    return (plank_frame_type_t)plank_ntohs(hdr->frame_type);
}

static inline uint16_t plank_frame_hdr_flags(const plank_frame_hdr_t* hdr) {
    return plank_ntohs(hdr->flags);
}

static inline uint32_t plank_frame_hdr_payload_len(const plank_frame_hdr_t* hdr) {
    return plank_ntohl(hdr->payload_len);
}

static inline uint64_t plank_frame_hdr_correlation_id(const plank_frame_hdr_t* hdr) {
    return plank_ntohll(hdr->correlation_id);
}

/* ============================================================================
 * BUNDLE HEADER HELPERS
 * ============================================================================ */

static inline void plank_bundle_hdr_init(plank_bundle_hdr_t* hdr,
                                         plank_bundle_type_t type,
                                         uint32_t flags,
                                         uint32_t manifest_len,
                                         uint32_t dir_count) {
    hdr->magic[0] = 'P';
    hdr->magic[1] = 'L';
    hdr->magic[2] = 'K';
    hdr->magic[3] = 'B';
    hdr->format_version = plank_htons(PLANK_BUNDLE_FORMAT_VERSION);
    hdr->bundle_type = plank_htons((uint16_t)type);
    hdr->flags = plank_htonl(flags);
    hdr->manifest_len = plank_htonl(manifest_len);
    hdr->dir_count = plank_htonl(dir_count);
    hdr->dir_entry_size = plank_htons(PLANK_DIRENT_SIZE);
    memset(hdr->reserved, 0, sizeof(hdr->reserved));
}

static inline bool plank_bundle_hdr_valid_magic(const plank_bundle_hdr_t* hdr) {
    return hdr->magic[0] == 'P' && hdr->magic[1] == 'L' &&
           hdr->magic[2] == 'K' && hdr->magic[3] == 'B';
}

static inline uint16_t plank_bundle_hdr_version(const plank_bundle_hdr_t* hdr) {
    return plank_ntohs(hdr->format_version);
}

static inline plank_bundle_type_t plank_bundle_hdr_type(const plank_bundle_hdr_t* hdr) {
    return (plank_bundle_type_t)plank_ntohs(hdr->bundle_type);
}

static inline uint32_t plank_bundle_hdr_flags(const plank_bundle_hdr_t* hdr) {
    return plank_ntohl(hdr->flags);
}

static inline uint32_t plank_bundle_hdr_manifest_len(const plank_bundle_hdr_t* hdr) {
    return plank_ntohl(hdr->manifest_len);
}

static inline uint32_t plank_bundle_hdr_dir_count(const plank_bundle_hdr_t* hdr) {
    return plank_ntohl(hdr->dir_count);
}

static inline uint16_t plank_bundle_hdr_dir_entry_size(const plank_bundle_hdr_t* hdr) {
    return plank_ntohs(hdr->dir_entry_size);
}

/* ============================================================================
 * DIRECTORY ENTRY HELPERS
 * ============================================================================ */

static inline void plank_dirent_init(plank_bundle_dirent_t* de,
                                     plank_record_type_t type,
                                     uint16_t flags,
                                     uint64_t offset,
                                     uint64_t encoded_len,
                                     uint64_t decoded_len,
                                     const uint8_t* digest,
                                     const uint8_t* record_id) {
    de->record_type = plank_htons((uint16_t)type);
    de->flags = plank_htons(flags);
    de->reserved = 0;
    de->offset = plank_htonll(offset);
    de->encoded_len = plank_htonll(encoded_len);
    de->decoded_len = plank_htonll(decoded_len);
    if (digest) {
        memcpy(de->digest, digest, 32);
    } else {
        memset(de->digest, 0, 32);
    }
    if (record_id) {
        memcpy(de->record_id, record_id, 24);
    } else {
        memset(de->record_id, 0, 24);
    }
}

static inline plank_record_type_t plank_dirent_type(const plank_bundle_dirent_t* de) {
    return (plank_record_type_t)plank_ntohs(de->record_type);
}

static inline uint16_t plank_dirent_flags(const plank_bundle_dirent_t* de) {
    return plank_ntohs(de->flags);
}

static inline uint64_t plank_dirent_offset(const plank_bundle_dirent_t* de) {
    return plank_ntohll(de->offset);
}

static inline uint64_t plank_dirent_encoded_len(const plank_bundle_dirent_t* de) {
    return plank_ntohll(de->encoded_len);
}

static inline uint64_t plank_dirent_decoded_len(const plank_bundle_dirent_t* de) {
    return plank_ntohll(de->decoded_len);
}

#ifdef __cplusplus
}
#endif

#endif /* PLANK_WIRE_H */
