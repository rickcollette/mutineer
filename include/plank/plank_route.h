/*
 * PLANK Routing Layer
 * Packet Link for Area Networked Knowledge
 *
 * Area routing, subscription management, deduplication, and loop prevention.
 */

#ifndef PLANK_ROUTE_H
#define PLANK_ROUTE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "plank_types.h"
#include "plank_store.h"
#include "plank_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ROUTING CONTEXT
 * ============================================================================ */

typedef struct plank_router plank_router_t;

/* Create router */
plank_router_t* plank_router_create(plank_store_t* store);

/* Free router */
void plank_router_free(plank_router_t* router);

/* ============================================================================
 * ROUTING DECISIONS
 * ============================================================================ */

typedef struct {
    int      link_id;
    int      priority;
    bool     eligible;
    char     reason[64];
} plank_route_decision_t;

/* Determine eligible links for area message */
int plank_router_route_area_message(plank_router_t* router,
                                    const plank_object_t* msg,
                                    const char* area_addr,
                                    plank_route_decision_t* decisions,
                                    int max_decisions);

/* Determine route for direct message */
bool plank_router_route_direct_message(plank_router_t* router,
                                       const plank_object_t* msg,
                                       const char* dest_node_addr,
                                       plank_route_decision_t* decision);

/* ============================================================================
 * SUBSCRIPTION MANAGEMENT
 * ============================================================================ */

/* Subscribe link to area */
bool plank_router_subscribe(plank_router_t* router, int link_id,
                            const char* area_addr);

/* Unsubscribe link from area */
bool plank_router_unsubscribe(plank_router_t* router, int link_id,
                              const char* area_addr);

/* Pause subscription */
bool plank_router_pause_subscription(plank_router_t* router, int link_id,
                                     const char* area_addr);

/* Resume subscription */
bool plank_router_resume_subscription(plank_router_t* router, int link_id,
                                      const char* area_addr);

/* Check if link is subscribed to area */
bool plank_router_is_subscribed(plank_router_t* router, int link_id,
                                const char* area_addr);

/* Get subscribed areas for link */
int plank_router_get_subscriptions(plank_router_t* router, int link_id,
                                   char** area_addrs, int max);

/* Get subscribed links for area */
int plank_router_get_subscribers(plank_router_t* router, const char* area_addr,
                                 int* link_ids, int max);

/* ============================================================================
 * LOOP PREVENTION
 * ============================================================================ */

/* Check if message would create a loop */
bool plank_router_check_loop(plank_router_t* router,
                             const plank_object_t* msg,
                             const uint8_t* local_node_id);

/* Check hop count against policy */
bool plank_router_check_hops(plank_router_t* router,
                             const plank_object_t* msg,
                             const char* area_addr);

/* Append local node to path */
bool plank_router_append_path(plank_router_t* router,
                              plank_message_body_t* msg_body,
                              const uint8_t* local_node_id);

/* ============================================================================
 * DEDUPLICATION
 * ============================================================================ */

/* Check if object is duplicate */
bool plank_router_is_duplicate(plank_router_t* router, const uint8_t* object_id);

/* Record object as seen */
bool plank_router_record_seen(plank_router_t* router, const uint8_t* object_id);

/* Check if bundle is duplicate */
bool plank_router_is_bundle_duplicate(plank_router_t* router,
                                      const uint8_t* bundle_id);

/* Record bundle as seen */
bool plank_router_record_bundle_seen(plank_router_t* router,
                                     const uint8_t* bundle_id);

/* Prune old dedup entries */
int plank_router_prune_dedupe(plank_router_t* router, int object_days,
                              int bundle_days);

/* ============================================================================
 * OUTBOUND QUEUE MANAGEMENT
 * ============================================================================ */

/* Queue object for outbound delivery */
bool plank_router_queue_outbound(plank_router_t* router,
                                 const plank_object_t* obj,
                                 int exclude_link_id);

/* Queue object to specific link */
bool plank_router_queue_to_link(plank_router_t* router,
                                const uint8_t* object_id,
                                int link_id,
                                int priority);

/* Get pending outbound count for link */
int plank_router_pending_count(plank_router_t* router, int link_id);

/* Get total pending outbound count */
int plank_router_total_pending(plank_router_t* router);

/* ============================================================================
 * CURSOR MANAGEMENT
 * ============================================================================ */

/* Get export cursor for link */
uint64_t plank_router_get_export_cursor(plank_router_t* router, int link_id);

/* Set export cursor for link */
bool plank_router_set_export_cursor(plank_router_t* router, int link_id,
                                    uint64_t cursor);

/* Get import cursor for link */
uint64_t plank_router_get_import_cursor(plank_router_t* router, int link_id);

/* Set import cursor for link */
bool plank_router_set_import_cursor(plank_router_t* router, int link_id,
                                    uint64_t cursor);

/* ============================================================================
 * DIRECT MESSAGE ROUTING
 * ============================================================================ */

/* Add route to node */
bool plank_router_add_route(plank_router_t* router,
                            const char* dest_node_addr,
                            int next_hop_link_id,
                            int hop_count,
                            int priority);

/* Remove route */
bool plank_router_remove_route(plank_router_t* router,
                               const char* dest_node_addr,
                               int next_hop_link_id);

/* Get best route to node */
bool plank_router_get_route(plank_router_t* router,
                            const char* dest_node_addr,
                            int* link_id_out,
                            int* hop_count_out);

/* ============================================================================
 * DEAD LETTER HANDLING
 * ============================================================================ */

/* Move failed delivery to dead letter */
bool plank_router_deadletter(plank_router_t* router,
                             int link_id,
                             const uint8_t** object_ids,
                             int count,
                             plank_error_code_t error_code,
                             const char* error_text);

/* Requeue dead letter items */
bool plank_router_requeue_deadletter(plank_router_t* router, int deadletter_id);

/* Abandon dead letter items */
bool plank_router_abandon_deadletter(plank_router_t* router, int deadletter_id);

/* ============================================================================
 * RETRY AND BACKOFF
 * ============================================================================ */

typedef struct {
    int      initial_sec;
    int      max_sec;
    int      limit;
    int      deadletter_timeout_sec;
} plank_retry_policy_t;

/* Get retry policy */
void plank_router_get_retry_policy(plank_router_t* router,
                                   plank_retry_policy_t* policy);

/* Set retry policy */
void plank_router_set_retry_policy(plank_router_t* router,
                                   const plank_retry_policy_t* policy);

/* Calculate next retry time */
int plank_router_calc_retry_delay(plank_router_t* router, int retry_count);

/* Check if should give up and deadletter */
bool plank_router_should_deadletter(plank_router_t* router, int retry_count,
                                    time_t first_failure);

/* ============================================================================
 * LINK HEALTH
 * ============================================================================ */

typedef struct {
    int      link_id;
    char     link_name[64];
    plank_link_state_t state;
    int      pending_count;
    int      retry_count;
    char     last_success[32];
    char     last_error[256];
    int      deadletter_count;
} plank_link_health_t;

/* Get link health */
bool plank_router_link_health(plank_router_t* router, int link_id,
                              plank_link_health_t* health);

/* Get all link health */
int plank_router_all_link_health(plank_router_t* router,
                                 plank_link_health_t* health, int max);

/* ============================================================================
 * AREA HEALTH
 * ============================================================================ */

typedef struct {
    char     area_addr[PLANK_MAX_ADDRESS];
    int      subscriber_count;
    int      pending_fanout;
    int      messages_today;
    char     last_message[32];
} plank_area_health_t;

/* Get area health */
bool plank_router_area_health(plank_router_t* router, const char* area_addr,
                              plank_area_health_t* health);

/* Get all area health */
int plank_router_all_area_health(plank_router_t* router,
                                 plank_area_health_t* health, int max);

#ifdef __cplusplus
}
#endif

#endif /* PLANK_ROUTE_H */
