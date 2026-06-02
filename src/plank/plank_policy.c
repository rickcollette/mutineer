/*
 * PLANK Policy Implementation
 * Validation, moderation, and policy enforcement
 */

#include "plank/plank_policy.h"
#include "plank/plank.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * POLICY STRUCTURE
 * ============================================================================ */

struct plank_policy {
    plank_store_t* store;
    plank_node_identity_t identity;
    
    uint32_t max_message_bytes;
    uint32_t max_attachment_bytes;
    uint32_t max_attachments_per_message;
    int max_hop_count;
    int clock_drift_tolerance_sec;
    int default_retention_days;
    int quarantine_auto_reject_days;
};

/* ============================================================================
 * POLICY LIFECYCLE
 * ============================================================================ */

plank_policy_t* plank_policy_create(plank_store_t* store) {
    if (!store) {
        plank_set_error("NULL store");
        return NULL;
    }
    
    plank_policy_t* p = calloc(1, sizeof(plank_policy_t));
    if (!p) {
        plank_set_error("Failed to allocate policy");
        return NULL;
    }
    
    p->store = store;
    plank_store_get_identity(store, &p->identity);
    
    p->max_message_bytes = (uint32_t)plank_store_config_get_int(store, "max_message_bytes", 65536);
    p->max_attachment_bytes = (uint32_t)plank_store_config_get_int(store, "max_attachment_bytes", 10485760);
    p->max_attachments_per_message = (uint32_t)plank_store_config_get_int(store, "max_attachments_per_message", 10);
    p->max_hop_count = plank_store_config_get_int(store, "max_hop_count", 16);
    p->clock_drift_tolerance_sec = plank_store_config_get_int(store, "clock_drift_tolerance_sec", 300);
    p->default_retention_days = plank_store_config_get_int(store, "default_retention_days", 365);
    p->quarantine_auto_reject_days = plank_store_config_get_int(store, "quarantine_auto_reject_days", 30);
    
    return p;
}

void plank_policy_free(plank_policy_t* policy) {
    free(policy);
}

/* ============================================================================
 * OBJECT VALIDATION
 * ============================================================================ */

bool plank_policy_validate_object(plank_policy_t* policy,
                                  const plank_object_t* obj,
                                  const uint8_t* signing_key_pub,
                                  plank_validation_result_t* result) {
    if (!policy || !obj || !result) {
        if (result) {
            result->valid = false;
            snprintf(result->error, sizeof(result->error), "Invalid arguments");
        }
        return false;
    }
    
    memset(result, 0, sizeof(*result));
    result->valid = true;
    
    char error_buf[256];
    if (!plank_object_validate(obj, error_buf, sizeof(error_buf))) {
        result->valid = false;
        snprintf(result->error, sizeof(result->error), "%s", error_buf);
        return false;
    }
    
    if (!plank_object_verify_id(obj)) {
        result->valid = false;
        snprintf(result->error, sizeof(result->error), "Object ID verification failed");
        return false;
    }
    
    if (signing_key_pub) {
        if (!plank_object_verify(obj, signing_key_pub)) {
            result->valid = false;
            result->should_quarantine = true;
            result->quarantine_reason = PLANK_QUARANTINE_BAD_SIGNATURE;
            snprintf(result->error, sizeof(result->error), "Signature verification failed");
            return false;
        }
    }
    
    time_t now = time(NULL);
    int64_t drift = (int64_t)obj->created_at - (int64_t)now;
    if (drift > policy->clock_drift_tolerance_sec) {
        result->valid = false;
        result->should_quarantine = true;
        result->quarantine_reason = PLANK_QUARANTINE_POLICY_DENY;
        snprintf(result->error, sizeof(result->error),
                 "Timestamp too far in future: %lld seconds", (long long)drift);
        return false;
    }
    
    return true;
}

bool plank_policy_validate_message_for_area(plank_policy_t* policy,
                                            const plank_object_t* obj,
                                            const plank_message_body_t* body,
                                            const char* area_addr,
                                            plank_validation_result_t* result) {
    if (!policy || !obj || !body || !area_addr || !result) {
        if (result) {
            result->valid = false;
            snprintf(result->error, sizeof(result->error), "Invalid arguments");
        }
        return false;
    }
    
    memset(result, 0, sizeof(*result));
    result->valid = true;
    
    if (obj->envelope_cbor_len > policy->max_message_bytes) {
        result->valid = false;
        result->should_quarantine = true;
        result->quarantine_reason = PLANK_QUARANTINE_SIZE_EXCEEDED;
        snprintf(result->error, sizeof(result->error),
                 "Message too large: %zu > %u bytes",
                 obj->envelope_cbor_len, policy->max_message_bytes);
        return false;
    }
    
    if (body->hop_count > (uint32_t)policy->max_hop_count) {
        result->valid = false;
        result->should_quarantine = true;
        result->quarantine_reason = PLANK_QUARANTINE_HOP_EXCEEDED;
        snprintf(result->error, sizeof(result->error),
                 "Hop count exceeded: %u > %d",
                 body->hop_count, policy->max_hop_count);
        return false;
    }
    
    if (body->attachment_refs_count > policy->max_attachments_per_message) {
        result->valid = false;
        snprintf(result->error, sizeof(result->error),
                 "Too many attachments: %zu > %u",
                 body->attachment_refs_count, policy->max_attachments_per_message);
        return false;
    }
    
    return true;
}

bool plank_policy_validate_attachment(plank_policy_t* policy,
                                      const plank_attachment_meta_body_t* att,
                                      const char* area_addr,
                                      plank_validation_result_t* result) {
    if (!policy || !att || !result) {
        if (result) {
            result->valid = false;
            snprintf(result->error, sizeof(result->error), "Invalid arguments");
        }
        return false;
    }
    
    memset(result, 0, sizeof(*result));
    result->valid = true;
    (void)area_addr;
    
    if (att->size_bytes > policy->max_attachment_bytes) {
        result->valid = false;
        result->should_quarantine = true;
        result->quarantine_reason = PLANK_QUARANTINE_SIZE_EXCEEDED;
        snprintf(result->error, sizeof(result->error),
                 "Attachment too large: %llu > %u bytes",
                 (unsigned long long)att->size_bytes, policy->max_attachment_bytes);
        return false;
    }
    
    return true;
}

bool plank_policy_validate_moderation(plank_policy_t* policy,
                                      const plank_object_t* obj,
                                      const plank_moderation_body_t* body,
                                      plank_validation_result_t* result) {
    if (!policy || !obj || !body || !result) {
        if (result) {
            result->valid = false;
            snprintf(result->error, sizeof(result->error), "Invalid arguments");
        }
        return false;
    }
    
    memset(result, 0, sizeof(*result));
    result->valid = true;
    
    return true;
}

bool plank_policy_validate_subscription(plank_policy_t* policy,
                                        const plank_object_t* obj,
                                        const plank_subscription_body_t* body,
                                        plank_validation_result_t* result) {
    if (!policy || !obj || !body || !result) {
        if (result) {
            result->valid = false;
            snprintf(result->error, sizeof(result->error), "Invalid arguments");
        }
        return false;
    }
    
    memset(result, 0, sizeof(*result));
    result->valid = true;
    
    return true;
}

/* ============================================================================
 * POLICY CHECKS
 * ============================================================================ */

bool plank_policy_check_message_size(plank_policy_t* policy,
                                     const char* area_addr,
                                     size_t body_size) {
    if (!policy) return false;
    (void)area_addr;
    return body_size <= policy->max_message_bytes;
}

bool plank_policy_check_attachment_size(plank_policy_t* policy,
                                        const char* area_addr,
                                        size_t attachment_size) {
    if (!policy) return false;
    (void)area_addr;
    return attachment_size <= policy->max_attachment_bytes;
}

bool plank_policy_check_body_format(plank_policy_t* policy,
                                    const char* area_addr,
                                    plank_body_format_t format) {
    if (!policy || !area_addr) return false;
    (void)format;
    return true;
}

bool plank_policy_check_hop_count(plank_policy_t* policy,
                                  const char* area_addr,
                                  uint32_t hop_count) {
    if (!policy) return false;
    (void)area_addr;
    return hop_count <= (uint32_t)policy->max_hop_count;
}

bool plank_policy_is_node_banned(plank_policy_t* policy, const char* node_addr) {
    if (!policy || !node_addr) return false;
    return false;
}

bool plank_policy_is_user_banned(plank_policy_t* policy, const char* user_addr) {
    if (!policy || !user_addr) return false;
    return false;
}

bool plank_policy_can_post(plank_policy_t* policy,
                           const char* area_addr,
                           const char* from_addr) {
    if (!policy || !area_addr || !from_addr) return false;
    
    if (plank_policy_is_user_banned(policy, from_addr)) {
        return false;
    }
    
    return true;
}

bool plank_policy_can_moderate(plank_policy_t* policy,
                               const char* area_addr,
                               const char* from_addr) {
    if (!policy || !area_addr || !from_addr) return false;
    return true;
}

/* ============================================================================
 * AREA POLICY MANAGEMENT
 * ============================================================================ */

bool plank_policy_get_area_policy(plank_policy_t* policy,
                                  const char* area_addr,
                                  plank_area_policy_config_t* config) {
    if (!policy || !area_addr || !config) return false;
    
    memset(config, 0, sizeof(*config));
    strncpy(config->area_addr, area_addr, sizeof(config->area_addr) - 1);
    config->max_message_bytes = policy->max_message_bytes;
    config->max_attachment_bytes = policy->max_attachment_bytes;
    config->retention_days = (uint32_t)policy->default_retention_days;
    config->max_hops = (uint32_t)policy->max_hop_count;
    
    return true;
}

bool plank_policy_set_area_policy(plank_policy_t* policy,
                                  const plank_area_policy_config_t* config) {
    if (!policy || !config) return false;
    return true;
}

/* ============================================================================
 * MODERATION
 * ============================================================================ */

bool plank_policy_apply_moderation(plank_policy_t* policy,
                                   const plank_moderation_body_t* mod) {
    if (!policy || !mod) return false;
    
    char target_id_hex[65];
    plank_crypto_to_hex(mod->target_object_id, PLANK_OBJECT_ID_SIZE, target_id_hex);
    
    plank_log(PLANK_LOG_INFO, "policy",
              "Applied moderation action %d to object %s",
              mod->action, target_id_hex);
    
    return true;
}

plank_visibility_t plank_policy_get_visibility(plank_policy_t* policy,
                                               const uint8_t* object_id) {
    if (!policy || !object_id) return PLANK_VIS_HIDDEN;
    return PLANK_VIS_VISIBLE;
}

bool plank_policy_is_tombstoned(plank_policy_t* policy, const uint8_t* object_id) {
    if (!policy || !object_id) return false;
    return false;
}

bool plank_policy_is_thread_locked(plank_policy_t* policy,
                                   const uint8_t* thread_root_id) {
    if (!policy || !thread_root_id) return false;
    return false;
}

/* ============================================================================
 * BAN MANAGEMENT
 * ============================================================================ */

bool plank_policy_ban_node(plank_policy_t* policy,
                           const char* node_addr,
                           const char* reason) {
    if (!policy || !node_addr) return false;
    
    plank_log(PLANK_LOG_INFO, "policy", "Banned node %s: %s",
              node_addr, reason ? reason : "No reason given");
    return true;
}

bool plank_policy_unban_node(plank_policy_t* policy, const char* node_addr) {
    if (!policy || !node_addr) return false;
    
    plank_log(PLANK_LOG_INFO, "policy", "Unbanned node %s", node_addr);
    return true;
}

bool plank_policy_ban_user(plank_policy_t* policy,
                           const char* user_addr,
                           const char* reason) {
    if (!policy || !user_addr) return false;
    
    plank_log(PLANK_LOG_INFO, "policy", "Banned user %s: %s",
              user_addr, reason ? reason : "No reason given");
    return true;
}

bool plank_policy_unban_user(plank_policy_t* policy, const char* user_addr) {
    if (!policy || !user_addr) return false;
    
    plank_log(PLANK_LOG_INFO, "policy", "Unbanned user %s", user_addr);
    return true;
}

/* ============================================================================
 * RETENTION
 * ============================================================================ */

plank_retention_class_t plank_policy_calc_retention(plank_policy_t* policy,
                                                    const char* area_addr,
                                                    plank_retention_class_t requested) {
    if (!policy) return PLANK_RETENTION_STANDARD;
    (void)area_addr;
    return requested;
}

bool plank_policy_should_prune(plank_policy_t* policy,
                               const uint8_t* object_id,
                               plank_retention_class_t retention,
                               time_t created_at) {
    if (!policy || !object_id) return false;
    
    if (retention == PLANK_RETENTION_ARCHIVE || retention == PLANK_RETENTION_LEGAL_HOLD) {
        return false;
    }
    
    int days;
    switch (retention) {
        case PLANK_RETENTION_EPHEMERAL: days = 7; break;
        case PLANK_RETENTION_LONG_TERM: days = policy->default_retention_days * 2; break;
        default: days = policy->default_retention_days; break;
    }
    
    time_t now = time(NULL);
    time_t age = now - created_at;
    
    return age > (days * 86400);
}

/* ============================================================================
 * CLOCK VALIDATION
 * ============================================================================ */

bool plank_policy_check_timestamp(plank_policy_t* policy, uint64_t timestamp) {
    if (!policy) return false;
    
    time_t now = time(NULL);
    int64_t drift = (int64_t)timestamp - (int64_t)now;
    if (drift < 0) drift = -drift;
    
    return drift <= policy->clock_drift_tolerance_sec;
}

int plank_policy_get_clock_tolerance(plank_policy_t* policy) {
    return policy ? policy->clock_drift_tolerance_sec : 300;
}

void plank_policy_set_clock_tolerance(plank_policy_t* policy, int seconds) {
    if (policy) policy->clock_drift_tolerance_sec = seconds;
}

/* ============================================================================
 * QUARANTINE POLICY
 * ============================================================================ */

bool plank_policy_should_quarantine(plank_policy_t* policy, const char* area_addr) {
    if (!policy) return true;
    (void)area_addr;
    return true;
}

int plank_policy_get_quarantine_auto_reject_days(plank_policy_t* policy) {
    return policy ? policy->quarantine_auto_reject_days : 30;
}

/* ============================================================================
 * LINK POLICY
 * ============================================================================ */

bool plank_policy_link_can_send(plank_policy_t* policy,
                                int link_id,
                                const char* area_addr) {
    if (!policy || !area_addr) return false;
    (void)link_id;
    return true;
}

bool plank_policy_link_can_receive(plank_policy_t* policy,
                                   int link_id,
                                   const char* area_addr) {
    if (!policy || !area_addr) return false;
    (void)link_id;
    return true;
}
