/*
 * PLANK Policy and Validation
 * Packet Link for Area Networked Knowledge
 *
 * Object validation, policy enforcement, and moderation.
 */

#ifndef PLANK_POLICY_H
#define PLANK_POLICY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"
#include "plank_object.h"
#include "plank_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * POLICY CONTEXT
 * ============================================================================ */

typedef struct plank_policy plank_policy_t;

/* Create policy context */
plank_policy_t* plank_policy_create(plank_store_t* store);

/* Free policy context */
void plank_policy_free(plank_policy_t* policy);

/* ============================================================================
 * VALIDATION RESULT
 * ============================================================================ */

typedef struct {
    bool     valid;
    bool     should_quarantine;
    plank_quarantine_reason_t quarantine_reason;
    char     error[256];
} plank_validation_result_t;

/* ============================================================================
 * OBJECT VALIDATION
 * ============================================================================ */

/* Validate object structure and signature */
bool plank_policy_validate_object(plank_policy_t* policy,
                                  const plank_object_t* obj,
                                  const uint8_t* signing_key_pub,
                                  plank_validation_result_t* result);

/* Validate message for area */
bool plank_policy_validate_message_for_area(plank_policy_t* policy,
                                            const plank_object_t* obj,
                                            const plank_message_body_t* body,
                                            const char* area_addr,
                                            plank_validation_result_t* result);

/* Validate attachment */
bool plank_policy_validate_attachment(plank_policy_t* policy,
                                      const plank_attachment_meta_body_t* att,
                                      const char* area_addr,
                                      plank_validation_result_t* result);

/* Validate moderation event */
bool plank_policy_validate_moderation(plank_policy_t* policy,
                                      const plank_object_t* obj,
                                      const plank_moderation_body_t* body,
                                      plank_validation_result_t* result);

/* Validate subscription event */
bool plank_policy_validate_subscription(plank_policy_t* policy,
                                        const plank_object_t* obj,
                                        const plank_subscription_body_t* body,
                                        plank_validation_result_t* result);

/* ============================================================================
 * POLICY CHECKS
 * ============================================================================ */

/* Check message size against area policy */
bool plank_policy_check_message_size(plank_policy_t* policy,
                                     const char* area_addr,
                                     size_t body_size);

/* Check attachment size against area policy */
bool plank_policy_check_attachment_size(plank_policy_t* policy,
                                        const char* area_addr,
                                        size_t attachment_size);

/* Check body format against area policy */
bool plank_policy_check_body_format(plank_policy_t* policy,
                                    const char* area_addr,
                                    plank_body_format_t format);

/* Check hop count against policy */
bool plank_policy_check_hop_count(plank_policy_t* policy,
                                  const char* area_addr,
                                  uint32_t hop_count);

/* Check if node is banned */
bool plank_policy_is_node_banned(plank_policy_t* policy,
                                 const char* node_addr);

/* Check if user is banned */
bool plank_policy_is_user_banned(plank_policy_t* policy,
                                 const char* user_addr);

/* Check posting permission */
bool plank_policy_can_post(plank_policy_t* policy,
                           const char* area_addr,
                           const char* from_addr);

/* Check moderation permission */
bool plank_policy_can_moderate(plank_policy_t* policy,
                               const char* area_addr,
                               const char* from_addr);

/* ============================================================================
 * AREA POLICY MANAGEMENT
 * ============================================================================ */

typedef struct {
    char     area_addr[PLANK_MAX_ADDRESS];
    plank_moderation_mode_t moderation_mode;
    uint32_t max_message_bytes;
    uint32_t max_attachment_bytes;
    uint32_t retention_days;
    uint32_t duplicate_window_days;
    uint32_t max_hops;
    bool     quarantine_on_violation;
    uint16_t* allowed_body_formats;
    size_t   allowed_body_formats_count;
} plank_area_policy_config_t;

/* Get area policy */
bool plank_policy_get_area_policy(plank_policy_t* policy,
                                  const char* area_addr,
                                  plank_area_policy_config_t* config);

/* Set area policy */
bool plank_policy_set_area_policy(plank_policy_t* policy,
                                  const plank_area_policy_config_t* config);

/* ============================================================================
 * MODERATION
 * ============================================================================ */

/* Apply moderation event */
bool plank_policy_apply_moderation(plank_policy_t* policy,
                                   const plank_moderation_body_t* mod);

/* Get effective visibility for object */
plank_visibility_t plank_policy_get_visibility(plank_policy_t* policy,
                                               const uint8_t* object_id);

/* Check if object is tombstoned */
bool plank_policy_is_tombstoned(plank_policy_t* policy,
                                const uint8_t* object_id);

/* Check if thread is locked */
bool plank_policy_is_thread_locked(plank_policy_t* policy,
                                   const uint8_t* thread_root_id);

/* ============================================================================
 * BAN MANAGEMENT
 * ============================================================================ */

/* Ban node */
bool plank_policy_ban_node(plank_policy_t* policy,
                           const char* node_addr,
                           const char* reason);

/* Unban node */
bool plank_policy_unban_node(plank_policy_t* policy,
                             const char* node_addr);

/* Ban user */
bool plank_policy_ban_user(plank_policy_t* policy,
                           const char* user_addr,
                           const char* reason);

/* Unban user */
bool plank_policy_unban_user(plank_policy_t* policy,
                             const char* user_addr);

/* ============================================================================
 * RETENTION
 * ============================================================================ */

/* Calculate retention class for message */
plank_retention_class_t plank_policy_calc_retention(plank_policy_t* policy,
                                                    const char* area_addr,
                                                    plank_retention_class_t requested);

/* Check if object should be pruned */
bool plank_policy_should_prune(plank_policy_t* policy,
                               const uint8_t* object_id,
                               plank_retention_class_t retention,
                               time_t created_at);

/* ============================================================================
 * CLOCK VALIDATION
 * ============================================================================ */

/* Check timestamp against clock tolerance */
bool plank_policy_check_timestamp(plank_policy_t* policy,
                                  uint64_t timestamp);

/* Get clock tolerance */
int plank_policy_get_clock_tolerance(plank_policy_t* policy);

/* Set clock tolerance */
void plank_policy_set_clock_tolerance(plank_policy_t* policy, int seconds);

/* ============================================================================
 * QUARANTINE POLICY
 * ============================================================================ */

/* Check if should quarantine on policy violation */
bool plank_policy_should_quarantine(plank_policy_t* policy,
                                    const char* area_addr);

/* Get quarantine auto-reject days */
int plank_policy_get_quarantine_auto_reject_days(plank_policy_t* policy);

/* ============================================================================
 * LINK POLICY
 * ============================================================================ */

/* Check if link is allowed to send to area */
bool plank_policy_link_can_send(plank_policy_t* policy,
                                int link_id,
                                const char* area_addr);

/* Check if link is allowed to receive from area */
bool plank_policy_link_can_receive(plank_policy_t* policy,
                                   int link_id,
                                   const char* area_addr);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_POLICY_H */
