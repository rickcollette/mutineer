/*
 * PLANK Routing Implementation
 * Area routing, subscription management, loop prevention, dead-letter handling
 */

#include "plank/plank_route.h"
#include "plank/plank.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ROUTER STRUCTURE
 * ============================================================================ */

struct plank_router
{
    plank_store_t *store;
    plank_node_identity_t identity;
    plank_retry_policy_t retry_policy;

    int max_hop_count;
    int clock_drift_tolerance_sec;
};

/* ============================================================================
 * ROUTER LIFECYCLE
 * ============================================================================ */

plank_router_t *plank_router_create(plank_store_t *store)
{
    if (!store)
    {
        plank_set_error("NULL store");
        return NULL;
    }

    plank_router_t *r = calloc(1, sizeof(plank_router_t));
    if (!r)
    {
        plank_set_error("Failed to allocate router");
        return NULL;
    }

    r->store = store;

    if (!plank_store_get_identity(store, &r->identity))
    {
        plank_log(PLANK_LOG_WARN, "router", "No identity found, using defaults");
    }

    r->max_hop_count = plank_store_config_get_int(store, "max_hop_count", 16);
    r->clock_drift_tolerance_sec = plank_store_config_get_int(store, "clock_drift_tolerance_sec", 300);

    r->retry_policy.initial_sec = plank_store_config_get_int(store, "retry_initial_sec", 60);
    r->retry_policy.max_sec = plank_store_config_get_int(store, "retry_max_sec", 3600);
    r->retry_policy.limit = plank_store_config_get_int(store, "retry_limit", 10);
    r->retry_policy.deadletter_timeout_sec = plank_store_config_get_int(store, "deadletter_timeout_sec", 86400);

    return r;
}

void plank_router_free(plank_router_t *router)
{
    free(router);
}

/* ============================================================================
 * ROUTING DECISIONS
 * ============================================================================ */

int plank_router_route_area_message(plank_router_t *router,
                                    const plank_object_t *msg,
                                    const char *area_addr,
                                    plank_route_decision_t *decisions,
                                    int max_decisions)
{
    if (!router || !msg || !area_addr || !decisions || max_decisions <= 0)
    {
        return 0;
    }

    (void)msg;

    plank_log(PLANK_LOG_DEBUG, "router", "Routing area message to %s", area_addr);
    return 0;
}

bool plank_router_route_direct_message(plank_router_t *router,
                                       const plank_object_t *msg,
                                       const char *dest_node_addr,
                                       plank_route_decision_t *decision)
{
    if (!router || !msg || !dest_node_addr || !decision)
    {
        return false;
    }

    memset(decision, 0, sizeof(*decision));

    char user[64], node[64], network[64];
    if (!plank_parse_user_addr(dest_node_addr, user, sizeof(user),
                               node, sizeof(node), network, sizeof(network)))
    {
        plank_log(PLANK_LOG_WARN, "router", "Invalid destination address: %s", dest_node_addr);
        return false;
    }

    if (strcmp(node, router->identity.node_name) == 0 &&
        strcmp(network, router->identity.network_name) == 0)
    {
        return false;
    }

    return false;
}

/* ============================================================================
 * SUBSCRIPTION MANAGEMENT
 * ============================================================================ */

bool plank_router_subscribe(plank_router_t *router, int link_id,
                            const char *area_addr)
{
    if (!router || !area_addr)
        return false;

    plank_log(PLANK_LOG_INFO, "router", "Subscribed link %d to area %s", link_id, area_addr);
    return true;
}

bool plank_router_unsubscribe(plank_router_t *router, int link_id,
                              const char *area_addr)
{
    if (!router || !area_addr)
        return false;

    plank_log(PLANK_LOG_INFO, "router", "Unsubscribed link %d from area %s", link_id, area_addr);
    return true;
}

bool plank_router_pause_subscription(plank_router_t *router, int link_id,
                                     const char *area_addr)
{
    if (!router || !area_addr)
        return false;
    (void)link_id;
    return true;
}

bool plank_router_resume_subscription(plank_router_t *router, int link_id,
                                      const char *area_addr)
{
    if (!router || !area_addr)
        return false;
    (void)link_id;
    return true;
}

bool plank_router_is_subscribed(plank_router_t *router, int link_id,
                                const char *area_addr)
{
    if (!router || !area_addr)
        return false;
    (void)link_id;
    return false;
}

int plank_router_get_subscriptions(plank_router_t *router, int link_id,
                                   char **area_addrs, int max)
{
    if (!router || !area_addrs || max <= 0)
        return 0;
    (void)link_id;
    return 0;
}

int plank_router_get_subscribers(plank_router_t *router, const char *area_addr,
                                 int *link_ids, int max)
{
    if (!router || !area_addr || !link_ids || max <= 0)
        return 0;
    return 0;
}

/* ============================================================================
 * LOOP PREVENTION
 * ============================================================================ */

bool plank_router_check_loop(plank_router_t *router,
                             const plank_object_t *msg,
                             const uint8_t *local_node_id)
{
    if (!router || !msg || !local_node_id)
        return true;

    if (msg->object_class == PLANK_CLASS_MESSAGE)
    {
        plank_message_body_t *body = plank_object_decode_message_body(msg);
        if (body)
        {
            for (size_t i = 0; i < body->path_count; i++)
            {
                if (memcmp(body->path[i], local_node_id, PLANK_NODE_ID_SIZE) == 0)
                {
                    plank_message_body_free(body);
                    plank_log(PLANK_LOG_WARN, "router", "Loop detected - node in path");
                    return true;
                }
            }
            plank_message_body_free(body);
        }
    }

    return false;
}

bool plank_router_check_hops(plank_router_t *router,
                             const plank_object_t *msg,
                             const char *area_addr)
{
    if (!router || !msg)
        return false;
    (void)area_addr;

    if (msg->object_class == PLANK_CLASS_MESSAGE)
    {
        plank_message_body_t *body = plank_object_decode_message_body(msg);
        if (body)
        {
            bool ok = body->hop_count <= (uint32_t)router->max_hop_count;
            if (!ok)
            {
                plank_log(PLANK_LOG_WARN, "router", "Hop count exceeded: %u > %d",
                          body->hop_count, router->max_hop_count);
            }
            plank_message_body_free(body);
            return ok;
        }
    }

    return true;
}

bool plank_router_append_path(plank_router_t *router,
                              plank_message_body_t *msg_body,
                              const uint8_t *local_node_id)
{
    if (!router || !msg_body || !local_node_id)
        return false;

    if (msg_body->path_count >= 16)
        return false;

    memcpy(msg_body->path[msg_body->path_count], local_node_id, PLANK_NODE_ID_SIZE);
    msg_body->path_count++;
    msg_body->hop_count++;

    return true;
}

/* ============================================================================
 * DEDUPLICATION
 * ============================================================================ */

bool plank_router_is_duplicate(plank_router_t *router, const uint8_t *object_id)
{
    if (!router || !object_id)
        return false;
    return plank_store_object_exists(router->store, object_id);
}

bool plank_router_record_seen(plank_router_t *router, const uint8_t *object_id)
{
    if (!router || !object_id)
        return false;
    return plank_store_dedupe_record(router->store, object_id);
}

bool plank_router_is_bundle_duplicate(plank_router_t *router, const uint8_t *bundle_id)
{
    if (!router || !bundle_id)
        return false;
    return plank_store_import_exists(router->store, bundle_id);
}

bool plank_router_record_bundle_seen(plank_router_t *router, const uint8_t *bundle_id)
{
    if (!router || !bundle_id)
        return false;
    return plank_store_dedupe_record(router->store, bundle_id);
}

int plank_router_prune_dedupe(plank_router_t *router, int object_days, int bundle_days)
{
    if (!router)
        return 0;
    (void)object_days;
    (void)bundle_days;
    return 0;
}

/* ============================================================================
 * OUTBOUND QUEUE
 * ============================================================================ */

bool plank_router_queue_outbound(plank_router_t *router,
                                 const plank_object_t *obj,
                                 int exclude_link_id)
{
    if (!router || !obj)
        return false;
    (void)exclude_link_id;
    return true;
}

bool plank_router_queue_to_link(plank_router_t *router,
                                const uint8_t *object_id,
                                int link_id,
                                int priority)
{
    if (!router || !object_id)
        return false;
    (void)link_id;
    (void)priority;
    return true;
}

int plank_router_pending_count(plank_router_t *router, int link_id)
{
    if (!router)
        return 0;
    (void)link_id;
    return 0;
}

int plank_router_total_pending(plank_router_t *router)
{
    if (!router)
        return 0;
    return 0;
}

/* ============================================================================
 * CURSOR MANAGEMENT
 * ============================================================================ */

uint64_t plank_router_get_export_cursor(plank_router_t *router, int link_id)
{
    if (!router)
        return 0;
    (void)link_id;
    return 0;
}

bool plank_router_set_export_cursor(plank_router_t *router, int link_id,
                                    uint64_t cursor)
{
    if (!router)
        return false;
    return plank_store_cursor_set_export(router->store, link_id, cursor);
}

uint64_t plank_router_get_import_cursor(plank_router_t *router, int link_id)
{
    if (!router)
        return 0;
    (void)link_id;
    return 0;
}

bool plank_router_set_import_cursor(plank_router_t *router, int link_id,
                                    uint64_t cursor)
{
    if (!router)
        return false;
    return plank_store_cursor_set_import(router->store, link_id, cursor);
}

/* ============================================================================
 * DIRECT MESSAGE ROUTING
 * ============================================================================ */

bool plank_router_add_route(plank_router_t *router,
                            const char *dest_node_addr,
                            int next_hop_link_id,
                            int hop_count,
                            int priority)
{
    if (!router || !dest_node_addr)
        return false;
    (void)next_hop_link_id;
    (void)hop_count;
    (void)priority;
    return true;
}

bool plank_router_remove_route(plank_router_t *router,
                               const char *dest_node_addr,
                               int next_hop_link_id)
{
    if (!router || !dest_node_addr)
        return false;
    (void)next_hop_link_id;
    return true;
}

bool plank_router_get_route(plank_router_t *router,
                            const char *dest_node_addr,
                            int *link_id_out,
                            int *hop_count_out)
{
    if (!router || !dest_node_addr)
        return false;
    if (link_id_out)
        *link_id_out = -1;
    if (hop_count_out)
        *hop_count_out = 0;
    return false;
}

/* ============================================================================
 * DEAD LETTER HANDLING
 * ============================================================================ */

bool plank_router_deadletter(plank_router_t *router,
                             int link_id,
                             const uint8_t **object_ids,
                             int count,
                             plank_error_code_t error_code,
                             const char *error_text)
{
    if (!router || !object_ids || count <= 0)
        return false;

    return plank_store_deadletter_add(router->store, link_id, NULL,
                                      object_ids, count, NULL, error_code, error_text);
}

bool plank_router_requeue_deadletter(plank_router_t *router, int deadletter_id)
{
    if (!router || deadletter_id <= 0)
        return false;
    return plank_store_deadletter_requeue(router->store, deadletter_id);
}

bool plank_router_abandon_deadletter(plank_router_t *router, int deadletter_id)
{
    if (!router || deadletter_id <= 0)
        return false;
    return plank_store_deadletter_abandon(router->store, deadletter_id);
}

/* ============================================================================
 * RETRY POLICY
 * ============================================================================ */

void plank_router_get_retry_policy(plank_router_t *router,
                                   plank_retry_policy_t *policy)
{
    if (!router || !policy)
        return;
    *policy = router->retry_policy;
}

void plank_router_set_retry_policy(plank_router_t *router,
                                   const plank_retry_policy_t *policy)
{
    if (!router || !policy)
        return;
    router->retry_policy = *policy;
}

int plank_router_calc_retry_delay(plank_router_t *router, int retry_count)
{
    if (!router)
        return 60;

    int delay = router->retry_policy.initial_sec * (1 << retry_count);
    if (delay > router->retry_policy.max_sec)
    {
        delay = router->retry_policy.max_sec;
    }

    int jitter = delay / 10;
    if (jitter > 0)
    {
        delay += (rand() % jitter) - (jitter / 2);
    }

    return delay;
}

bool plank_router_should_deadletter(plank_router_t *router, int retry_count,
                                    time_t first_failure)
{
    if (!router)
        return true;

    if (retry_count >= router->retry_policy.limit)
        return true;

    time_t now = time(NULL);
    if ((now - first_failure) > router->retry_policy.deadletter_timeout_sec)
        return true;

    return false;
}

/* ============================================================================
 * HEALTH REPORTING
 * ============================================================================ */

bool plank_router_link_health(plank_router_t *router, int link_id,
                              plank_link_health_t *health)
{
    if (!router || !health)
        return false;

    memset(health, 0, sizeof(*health));
    health->link_id = link_id;
    health->state = PLANK_LINK_IDLE;

    return true;
}

int plank_router_all_link_health(plank_router_t *router,
                                 plank_link_health_t *health, int max)
{
    if (!router || !health || max <= 0)
        return 0;
    return 0;
}

bool plank_router_area_health(plank_router_t *router, const char *area_addr,
                              plank_area_health_t *health)
{
    if (!router || !area_addr || !health)
        return false;

    memset(health, 0, sizeof(*health));
    strncpy(health->area_addr, area_addr, sizeof(health->area_addr) - 1);

    return true;
}

int plank_router_all_area_health(plank_router_t *router,
                                 plank_area_health_t *health, int max)
{
    if (!router || !health || max <= 0)
        return 0;
    return 0;
}
