/*
 * PLANK - Packet Link for Area Networked Knowledge
 * Main header file for the Mutineer BBS networking subsystem
 *
 * PLANK is the native packet mail and area distribution network for Mutineer.
 * COVE (Central Offline Vertex Exchange) is the hub/redistribution role.
 *
 * This header includes all PLANK components.
 */

#ifndef PLANK_H
#define PLANK_H

/* Core types and enums */
#include "plank_types.h"

/* Wire protocol structures */
#include "plank_wire.h"

/* Object model */
#include "plank_object.h"

/* CBOR encoding/decoding */
#include "plank_cbor.h"

/* Cryptographic operations */
#include "plank_crypto.h"

/* Database storage */
#include "plank_store.h"

/* Bundle operations */
#include "plank_bundle.h"

/* Link protocol */
#include "plank_link.h"

/* Routing layer */
#include "plank_route.h"

/* Policy and validation */
#include "plank_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION INFO
 * ============================================================================ */

#define PLANK_VERSION_MAJOR 1
#define PLANK_VERSION_MINOR 0
#define PLANK_VERSION_PATCH 0
#define PLANK_VERSION_STRING "1.0.0"

/* Get version string */
const char* plank_version(void);

/* Get protocol version */
uint16_t plank_protocol_version(void);

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* Initialize PLANK subsystem (call once at startup) */
bool plank_init(void);

/* Shutdown PLANK subsystem */
void plank_shutdown(void);

/* ============================================================================
 * ADDRESS FORMATTING
 * ============================================================================ */

/* Format node address: "node_name@network_name" */
bool plank_format_node_addr(const char* node_name, const char* network_name,
                            char* addr_out, size_t max);

/* Format user address: "user@node_name@network_name" */
bool plank_format_user_addr(const char* user, const char* node_name,
                            const char* network_name, char* addr_out, size_t max);

/* Format area address: "slug@origin_node@network_name" */
bool plank_format_area_addr(const char* slug, const char* origin_node,
                            const char* network_name, char* addr_out, size_t max);

/* Parse node address */
bool plank_parse_node_addr(const char* addr, char* node_name, size_t node_max,
                           char* network_name, size_t network_max);

/* Parse user address */
bool plank_parse_user_addr(const char* addr, char* user, size_t user_max,
                           char* node_name, size_t node_max,
                           char* network_name, size_t network_max);

/* Parse area address */
bool plank_parse_area_addr(const char* addr, char* slug, size_t slug_max,
                           char* origin_node, size_t origin_max,
                           char* network_name, size_t network_max);

/* Validate address format */
bool plank_validate_node_addr(const char* addr);
bool plank_validate_user_addr(const char* addr);
bool plank_validate_area_addr(const char* addr);

/* Canonicalize address (lowercase) */
void plank_canonicalize_addr(char* addr);

/* ============================================================================
 * ERROR HANDLING
 * ============================================================================ */

/* Get last error message */
const char* plank_last_error(void);

/* Set error message (internal use) */
void plank_set_error(const char* fmt, ...);

/* Clear error */
void plank_clear_error(void);

/* ============================================================================
 * LOGGING
 * ============================================================================ */

typedef enum {
    PLANK_LOG_DEBUG = 0,
    PLANK_LOG_INFO  = 1,
    PLANK_LOG_WARN  = 2,
    PLANK_LOG_ERROR = 3
} plank_log_level_t;

/* Log callback type */
typedef void (*plank_log_callback_t)(plank_log_level_t level,
                                     const char* component,
                                     const char* message,
                                     void* ctx);

/* Set log callback */
void plank_set_log_callback(plank_log_callback_t cb, void* ctx);

/* Set minimum log level */
void plank_set_log_level(plank_log_level_t level);

/* Log message (internal use) */
void plank_log(plank_log_level_t level, const char* component,
               const char* fmt, ...);

/* ============================================================================
 * UTILITY MACROS
 * ============================================================================ */

#define PLANK_LOG_DEBUG(comp, ...) plank_log(PLANK_LOG_DEBUG, comp, __VA_ARGS__)
#define PLANK_LOG_INFO(comp, ...)  plank_log(PLANK_LOG_INFO, comp, __VA_ARGS__)
#define PLANK_LOG_WARN(comp, ...)  plank_log(PLANK_LOG_WARN, comp, __VA_ARGS__)
#define PLANK_LOG_ERROR(comp, ...) plank_log(PLANK_LOG_ERROR, comp, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* PLANK_H */
