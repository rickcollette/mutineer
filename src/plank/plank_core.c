/*
 * PLANK Core Implementation
 * Object model, address handling, initialization
 */

#include "plank/plank.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

static bool g_initialized = false;
static char g_last_error[512] = {0};
static plank_log_callback_t g_log_callback = NULL;
static void* g_log_ctx = NULL;
static plank_log_level_t g_log_level = PLANK_LOG_INFO;

/* ============================================================================
 * VERSION INFO
 * ============================================================================ */

const char* plank_version(void) {
    return PLANK_VERSION_STRING;
}

uint16_t plank_protocol_version(void) {
    return PLANK_PROTOCOL_VERSION;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

bool plank_init(void) {
    if (g_initialized) return true;
    
    /* OpenSSL initialization is automatic in modern versions */
    
    g_initialized = true;
    return true;
}

void plank_shutdown(void) {
    g_initialized = false;
}

/* ============================================================================
 * ERROR HANDLING
 * ============================================================================ */

const char* plank_last_error(void) {
    return g_last_error;
}

void plank_set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
}

void plank_clear_error(void) {
    g_last_error[0] = '\0';
}

/* ============================================================================
 * LOGGING
 * ============================================================================ */

void plank_set_log_callback(plank_log_callback_t cb, void* ctx) {
    g_log_callback = cb;
    g_log_ctx = ctx;
}

void plank_set_log_level(plank_log_level_t level) {
    g_log_level = level;
}

void plank_log(plank_log_level_t level, const char* component,
               const char* fmt, ...) {
    if (level < g_log_level) return;
    
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    if (g_log_callback) {
        g_log_callback(level, component, message, g_log_ctx);
    } else {
        const char* level_str = "???";
        switch (level) {
            case PLANK_LOG_DEBUG: level_str = "DEBUG"; break;
            case PLANK_LOG_INFO:  level_str = "INFO"; break;
            case PLANK_LOG_WARN:  level_str = "WARN"; break;
            case PLANK_LOG_ERROR: level_str = "ERROR"; break;
        }
        fprintf(stderr, "[%s] %s: %s\n", level_str, component, message);
    }
}

/* ============================================================================
 * ADDRESS VALIDATION HELPERS
 * ============================================================================ */

static bool is_valid_name_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static bool is_valid_user_char(char c) {
    return is_valid_name_char(c) || c == '.';
}

static bool is_valid_slug_char(char c) {
    return is_valid_name_char(c) || c == '.';
}

static bool validate_name(const char* name, size_t max_len) {
    if (!name || !*name) return false;
    size_t len = strlen(name);
    if (len > max_len) return false;
    for (size_t i = 0; i < len; i++) {
        if (!is_valid_name_char(name[i])) return false;
    }
    return true;
}

/* ============================================================================
 * ADDRESS FORMATTING
 * ============================================================================ */

bool plank_format_node_addr(const char* node_name, const char* network_name,
                            char* addr_out, size_t max) {
    if (!node_name || !network_name || !addr_out) {
        plank_set_error("Invalid arguments to plank_format_node_addr");
        return false;
    }
    
    if (!validate_name(node_name, PLANK_MAX_NODE_NAME)) {
        plank_set_error("Invalid node name");
        return false;
    }
    
    if (!validate_name(network_name, PLANK_MAX_NETWORK_NAME)) {
        plank_set_error("Invalid network name");
        return false;
    }
    
    int written = snprintf(addr_out, max, "%s@%s", node_name, network_name);
    if (written < 0 || (size_t)written >= max) {
        plank_set_error("Address buffer too small");
        return false;
    }
    
    plank_canonicalize_addr(addr_out);
    return true;
}

bool plank_format_user_addr(const char* user, const char* node_name,
                            const char* network_name, char* addr_out, size_t max) {
    if (!user || !node_name || !network_name || !addr_out) {
        plank_set_error("Invalid arguments to plank_format_user_addr");
        return false;
    }
    
    size_t user_len = strlen(user);
    if (user_len == 0 || user_len > PLANK_MAX_USER_NAME) {
        plank_set_error("Invalid user name length");
        return false;
    }
    for (size_t i = 0; i < user_len; i++) {
        if (!is_valid_user_char(user[i])) {
            plank_set_error("Invalid character in user name");
            return false;
        }
    }
    
    if (!validate_name(node_name, PLANK_MAX_NODE_NAME)) {
        plank_set_error("Invalid node name");
        return false;
    }
    
    if (!validate_name(network_name, PLANK_MAX_NETWORK_NAME)) {
        plank_set_error("Invalid network name");
        return false;
    }
    
    int written = snprintf(addr_out, max, "%s@%s@%s", user, node_name, network_name);
    if (written < 0 || (size_t)written >= max) {
        plank_set_error("Address buffer too small");
        return false;
    }
    
    plank_canonicalize_addr(addr_out);
    return true;
}

bool plank_format_area_addr(const char* slug, const char* origin_node,
                            const char* network_name, char* addr_out, size_t max) {
    if (!slug || !origin_node || !network_name || !addr_out) {
        plank_set_error("Invalid arguments to plank_format_area_addr");
        return false;
    }
    
    size_t slug_len = strlen(slug);
    if (slug_len == 0 || slug_len > PLANK_MAX_AREA_SLUG) {
        plank_set_error("Invalid area slug length");
        return false;
    }
    for (size_t i = 0; i < slug_len; i++) {
        if (!is_valid_slug_char(slug[i])) {
            plank_set_error("Invalid character in area slug");
            return false;
        }
    }
    
    if (!validate_name(origin_node, PLANK_MAX_NODE_NAME)) {
        plank_set_error("Invalid origin node name");
        return false;
    }
    
    if (!validate_name(network_name, PLANK_MAX_NETWORK_NAME)) {
        plank_set_error("Invalid network name");
        return false;
    }
    
    int written = snprintf(addr_out, max, "%s@%s@%s", slug, origin_node, network_name);
    if (written < 0 || (size_t)written >= max) {
        plank_set_error("Address buffer too small");
        return false;
    }
    
    plank_canonicalize_addr(addr_out);
    return true;
}

/* ============================================================================
 * ADDRESS PARSING
 * ============================================================================ */

bool plank_parse_node_addr(const char* addr, char* node_name, size_t node_max,
                           char* network_name, size_t network_max) {
    if (!addr) return false;
    
    const char* at = strchr(addr, '@');
    if (!at) return false;
    
    size_t node_len = at - addr;
    if (node_len == 0 || node_len >= node_max) return false;
    
    size_t net_len = strlen(at + 1);
    if (net_len == 0 || net_len >= network_max) return false;
    
    /* Check for extra @ (would be user addr) */
    if (strchr(at + 1, '@')) return false;
    
    memcpy(node_name, addr, node_len);
    node_name[node_len] = '\0';
    
    memcpy(network_name, at + 1, net_len);
    network_name[net_len] = '\0';
    
    return true;
}

bool plank_parse_user_addr(const char* addr, char* user, size_t user_max,
                           char* node_name, size_t node_max,
                           char* network_name, size_t network_max) {
    if (!addr) return false;
    
    const char* at1 = strchr(addr, '@');
    if (!at1) return false;
    
    const char* at2 = strchr(at1 + 1, '@');
    if (!at2) return false;
    
    /* Check for extra @ */
    if (strchr(at2 + 1, '@')) return false;
    
    size_t user_len = at1 - addr;
    if (user_len == 0 || user_len >= user_max) return false;
    
    size_t node_len = at2 - at1 - 1;
    if (node_len == 0 || node_len >= node_max) return false;
    
    size_t net_len = strlen(at2 + 1);
    if (net_len == 0 || net_len >= network_max) return false;
    
    memcpy(user, addr, user_len);
    user[user_len] = '\0';
    
    memcpy(node_name, at1 + 1, node_len);
    node_name[node_len] = '\0';
    
    memcpy(network_name, at2 + 1, net_len);
    network_name[net_len] = '\0';
    
    return true;
}

bool plank_parse_area_addr(const char* addr, char* slug, size_t slug_max,
                           char* origin_node, size_t origin_max,
                           char* network_name, size_t network_max) {
    /* Same format as user address */
    return plank_parse_user_addr(addr, slug, slug_max,
                                 origin_node, origin_max,
                                 network_name, network_max);
}

/* ============================================================================
 * ADDRESS VALIDATION
 * ============================================================================ */

bool plank_validate_node_addr(const char* addr) {
    char node[PLANK_MAX_NODE_NAME + 1];
    char network[PLANK_MAX_NETWORK_NAME + 1];
    return plank_parse_node_addr(addr, node, sizeof(node),
                                 network, sizeof(network));
}

bool plank_validate_user_addr(const char* addr) {
    char user[PLANK_MAX_USER_NAME + 1];
    char node[PLANK_MAX_NODE_NAME + 1];
    char network[PLANK_MAX_NETWORK_NAME + 1];
    return plank_parse_user_addr(addr, user, sizeof(user),
                                 node, sizeof(node),
                                 network, sizeof(network));
}

bool plank_validate_area_addr(const char* addr) {
    char slug[PLANK_MAX_AREA_SLUG + 1];
    char origin[PLANK_MAX_NODE_NAME + 1];
    char network[PLANK_MAX_NETWORK_NAME + 1];
    return plank_parse_area_addr(addr, slug, sizeof(slug),
                                 origin, sizeof(origin),
                                 network, sizeof(network));
}

void plank_canonicalize_addr(char* addr) {
    if (!addr) return;
    for (char* p = addr; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

/* ============================================================================
 * OBJECT LIFECYCLE
 * ============================================================================ */

plank_object_t* plank_object_new(void) {
    plank_object_t* obj = calloc(1, sizeof(plank_object_t));
    if (obj) {
        obj->version = 1;
        obj->sig_alg = PLANK_SIG_ED25519;
    }
    return obj;
}

void plank_object_free(plank_object_t* obj) {
    if (!obj) return;
    free(obj->body_cbor);
    free(obj->envelope_cbor);
    free(obj);
}

plank_object_t* plank_object_clone(const plank_object_t* obj) {
    if (!obj) return NULL;
    
    plank_object_t* clone = plank_object_new();
    if (!clone) return NULL;
    
    memcpy(clone, obj, sizeof(plank_object_t));
    clone->body_cbor = NULL;
    clone->envelope_cbor = NULL;
    
    if (obj->body_cbor && obj->body_cbor_len > 0) {
        clone->body_cbor = malloc(obj->body_cbor_len);
        if (!clone->body_cbor) {
            plank_object_free(clone);
            return NULL;
        }
        memcpy(clone->body_cbor, obj->body_cbor, obj->body_cbor_len);
        clone->body_cbor_len = obj->body_cbor_len;
    }
    
    if (obj->envelope_cbor && obj->envelope_cbor_len > 0) {
        clone->envelope_cbor = malloc(obj->envelope_cbor_len);
        if (!clone->envelope_cbor) {
            plank_object_free(clone);
            return NULL;
        }
        memcpy(clone->envelope_cbor, obj->envelope_cbor, obj->envelope_cbor_len);
        clone->envelope_cbor_len = obj->envelope_cbor_len;
    }
    
    return clone;
}

/* ============================================================================
 * MESSAGE BODY LIFECYCLE
 * ============================================================================ */

plank_message_body_t* plank_message_body_new(void) {
    plank_message_body_t* body = calloc(1, sizeof(plank_message_body_t));
    if (body) {
        body->message_type = PLANK_MSG_AREA_POST;
        body->body_format = PLANK_BODY_PLAIN_UTF8;
        body->retention_class = PLANK_RETENTION_STANDARD;
        body->visibility = PLANK_VIS_VISIBLE;
    }
    return body;
}

void plank_message_body_free(plank_message_body_t* body) {
    if (!body) return;
    
    if (body->to_addrs) {
        for (size_t i = 0; i < body->to_addrs_count; i++) {
            free(body->to_addrs[i]);
        }
        free(body->to_addrs);
    }
    
    free(body->body_text);
    
    if (body->attachment_refs) {
        for (size_t i = 0; i < body->attachment_refs_count; i++) {
            free(body->attachment_refs[i]);
        }
        free(body->attachment_refs);
    }
    
    if (body->path) {
        for (size_t i = 0; i < body->path_count; i++) {
            free(body->path[i]);
        }
        free(body->path);
    }
    
    free(body);
}

plank_message_body_t* plank_message_body_clone(const plank_message_body_t* body) {
    if (!body) return NULL;
    
    plank_message_body_t* clone = plank_message_body_new();
    if (!clone) return NULL;
    
    clone->message_type = body->message_type;
    memcpy(clone->author_user, body->author_user, sizeof(clone->author_user));
    memcpy(clone->author_display, body->author_display, sizeof(clone->author_display));
    memcpy(clone->from_addr, body->from_addr, sizeof(clone->from_addr));
    memcpy(clone->area_addr, body->area_addr, sizeof(clone->area_addr));
    memcpy(clone->subject, body->subject, sizeof(clone->subject));
    clone->body_format = body->body_format;
    memcpy(clone->thread_root_id, body->thread_root_id, sizeof(clone->thread_root_id));
    memcpy(clone->parent_id, body->parent_id, sizeof(clone->parent_id));
    memcpy(clone->reply_to, body->reply_to, sizeof(clone->reply_to));
    clone->retention_class = body->retention_class;
    clone->visibility = body->visibility;
    clone->flags = body->flags;
    clone->hop_count = body->hop_count;
    
    if (body->body_text && body->body_text_len > 0) {
        clone->body_text = malloc(body->body_text_len + 1);
        if (!clone->body_text) {
            plank_message_body_free(clone);
            return NULL;
        }
        memcpy(clone->body_text, body->body_text, body->body_text_len);
        clone->body_text[body->body_text_len] = '\0';
        clone->body_text_len = body->body_text_len;
    }
    
    /* Clone to_addrs */
    if (body->to_addrs && body->to_addrs_count > 0) {
        clone->to_addrs = calloc(body->to_addrs_count, sizeof(char*));
        if (!clone->to_addrs) {
            plank_message_body_free(clone);
            return NULL;
        }
        for (size_t i = 0; i < body->to_addrs_count; i++) {
            clone->to_addrs[i] = strdup(body->to_addrs[i]);
            if (!clone->to_addrs[i]) {
                plank_message_body_free(clone);
                return NULL;
            }
        }
        clone->to_addrs_count = body->to_addrs_count;
    }
    
    /* Clone attachment_refs */
    if (body->attachment_refs && body->attachment_refs_count > 0) {
        clone->attachment_refs = calloc(body->attachment_refs_count, sizeof(uint8_t*));
        if (!clone->attachment_refs) {
            plank_message_body_free(clone);
            return NULL;
        }
        for (size_t i = 0; i < body->attachment_refs_count; i++) {
            clone->attachment_refs[i] = malloc(PLANK_ATTACHMENT_ID_SIZE);
            if (!clone->attachment_refs[i]) {
                plank_message_body_free(clone);
                return NULL;
            }
            memcpy(clone->attachment_refs[i], body->attachment_refs[i],
                   PLANK_ATTACHMENT_ID_SIZE);
        }
        clone->attachment_refs_count = body->attachment_refs_count;
    }
    
    /* Clone path */
    if (body->path && body->path_count > 0) {
        clone->path = calloc(body->path_count, sizeof(uint8_t*));
        if (!clone->path) {
            plank_message_body_free(clone);
            return NULL;
        }
        for (size_t i = 0; i < body->path_count; i++) {
            clone->path[i] = malloc(PLANK_NODE_ID_SIZE);
            if (!clone->path[i]) {
                plank_message_body_free(clone);
                return NULL;
            }
            memcpy(clone->path[i], body->path[i], PLANK_NODE_ID_SIZE);
        }
        clone->path_count = body->path_count;
    }
    
    return clone;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

void plank_object_id_to_hex(const uint8_t* id, char* hex_out) {
    plank_crypto_to_hex(id, PLANK_OBJECT_ID_SIZE, hex_out);
}

bool plank_hex_to_object_id(const char* hex, uint8_t* id_out) {
    if (strlen(hex) != PLANK_OBJECT_ID_SIZE * 2) return false;
    return plank_crypto_from_hex(hex, id_out, PLANK_OBJECT_ID_SIZE);
}

const char* plank_object_class_name(plank_object_class_t cls) {
    switch (cls) {
        case PLANK_CLASS_AREA_DEFINITION:   return "AreaDefinition";
        case PLANK_CLASS_AREA_POLICY:       return "AreaPolicy";
        case PLANK_CLASS_MESSAGE:           return "Message";
        case PLANK_CLASS_ATTACHMENT_META:   return "AttachmentMeta";
        case PLANK_CLASS_SUBSCRIPTION:      return "SubscriptionEvent";
        case PLANK_CLASS_MODERATION:        return "ModerationEvent";
        case PLANK_CLASS_ROUTING:           return "RoutingEvent";
        case PLANK_CLASS_RECEIPT:           return "ReceiptEvent";
        case PLANK_CLASS_LINK_EVENT:        return "LinkEvent";
        case PLANK_CLASS_BUNDLE_CHECKPOINT: return "BundleCheckpoint";
        case PLANK_CLASS_NODE_INFO:         return "NodeInfo";
        default:                            return "Unknown";
    }
}

const char* plank_message_type_name(plank_message_type_t type) {
    switch (type) {
        case PLANK_MSG_AREA_POST:   return "AREA_POST";
        case PLANK_MSG_DIRECT_POST: return "DIRECT_POST";
        case PLANK_MSG_SYSTEM_POST: return "SYSTEM_POST";
        default:                    return "Unknown";
    }
}

bool plank_object_id_equal(const uint8_t* a, const uint8_t* b) {
    return plank_crypto_memcmp(a, b, PLANK_OBJECT_ID_SIZE);
}

bool plank_object_id_is_zero(const uint8_t* id) {
    for (size_t i = 0; i < PLANK_OBJECT_ID_SIZE; i++) {
        if (id[i] != 0) return false;
    }
    return true;
}
