/*
 * PLANK Routing Tests
 * Tests for routing logic, deduplication, and queue management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "plank/plank.h"
#include "plank/plank_route.h"
#include "plank/plank_store.h"
#include "plank/plank_object.h"
#include "plank/plank_crypto.h"
#include "plank/plank_types.h"
#include "bbs_db.h"

static char test_db_path[256];
static BbsDb *test_db = NULL;
static plank_store_t *test_store = NULL;
static plank_router_t *test_router = NULL;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  %-50s", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    Expected %lld == %lld\n    at %s:%d\n", \
               (long long)(a), (long long)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

static void setup(void) {
    snprintf(test_db_path, sizeof(test_db_path), "%s/plank_route_test_%d.db",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());

    test_db = db_open(test_db_path);
    ASSERT(test_db != NULL);

    test_store = plank_store_open(test_db);
    ASSERT(test_store != NULL);

    bool ok = plank_store_init_schema(test_store, "sql/plank_schema.sql");
    ASSERT(ok);
    db_exec(test_db, "PRAGMA foreign_keys=OFF");

    plank_store_generate_identity(test_store, "routetest", "testnet");

    test_router = plank_router_create(test_store);
    ASSERT(test_router != NULL);
}

static void teardown(void) {
    if (test_router) {
        plank_router_free(test_router);
        test_router = NULL;
    }
    if (test_store) {
        plank_store_close(test_store);
        test_store = NULL;
    }
    if (test_db) {
        db_close(test_db);
        test_db = NULL;
    }
    unlink(test_db_path);
}

/* Helper: create a signed test message using the modern API */
static plank_object_t *create_test_message(const char *area, const char *subject) {
    uint8_t pubkey[32], privkey[32];
    plank_crypto_keygen_ed25519(pubkey, privkey);

    plank_message_body_t *body = plank_message_body_new();
    ASSERT(body != NULL);
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->area_addr, area, sizeof(body->area_addr) - 1);
    strncpy(body->subject, subject, sizeof(body->subject) - 1);
    strncpy(body->author_user, "testuser", sizeof(body->author_user) - 1);
    body->body_text = strdup("Test body");
    body->body_text_len = strlen(body->body_text);

    uint8_t node_id[PLANK_NODE_ID_SIZE] = {0};
    plank_object_t *obj = plank_object_create_message(
        node_id, "routetest@testnet", body, privkey);
    plank_message_body_free(body);
    return obj;
}

TEST(router_create) {
    ASSERT(test_router != NULL);
}

TEST(duplicate_detection) {
    /* Object-level: store object → is_duplicate returns true */
    plank_object_t *obj = create_test_message("general@routetest@testnet", "Dup Check");
    ASSERT(obj != NULL);

    /* Not in store yet */
    bool is_dup = plank_router_is_duplicate(test_router, obj->object_id);
    ASSERT(!is_dup);

    /* Store it */
    int64_t seq = 0;
    bool ok = plank_store_object_put(test_store, obj, PLANK_SOURCE_LOCAL, 0, &seq);
    ASSERT(ok);

    /* Now it's a duplicate */
    is_dup = plank_router_is_duplicate(test_router, obj->object_id);
    ASSERT(is_dup);
    plank_object_free(obj);

    /* Store-level dedupe table (used for seen-but-not-stored tracking) */
    uint8_t obj_id[PLANK_OBJECT_ID_SIZE];
    plank_crypto_random(obj_id, sizeof(obj_id));
    ASSERT(!plank_store_dedupe_exists(test_store, obj_id));
    ok = plank_store_dedupe_record(test_store, obj_id);
    ASSERT(ok);
    ASSERT(plank_store_dedupe_exists(test_store, obj_id));
}

TEST(hop_count_validation) {
    /* Create a message with hop_count=5 (well within default limit of 16) */
    uint8_t pubkey[32], privkey[32];
    plank_crypto_keygen_ed25519(pubkey, privkey);

    plank_message_body_t *body = plank_message_body_new();
    ASSERT(body != NULL);
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->area_addr, "general@routetest@testnet", sizeof(body->area_addr) - 1);
    strncpy(body->subject, "Hop Test", sizeof(body->subject) - 1);
    strncpy(body->author_user, "testuser", sizeof(body->author_user) - 1);
    body->body_text = strdup("body");
    body->body_text_len = strlen(body->body_text);
    body->hop_count = 5;

    uint8_t node_id[PLANK_NODE_ID_SIZE] = {0};
    plank_object_t *obj = plank_object_create_message(node_id, "routetest@testnet", body, privkey);
    plank_message_body_free(body);
    ASSERT(obj != NULL);

    /* hop_count=5, max=16: should pass */
    bool ok = plank_router_check_hops(test_router, obj, "general@routetest@testnet");
    ASSERT(ok);
    plank_object_free(obj);

    /* Create a message with hop_count=100 (exceeds default limit) */
    body = plank_message_body_new();
    ASSERT(body != NULL);
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->area_addr, "general@routetest@testnet", sizeof(body->area_addr) - 1);
    strncpy(body->subject, "Hop Exceed Test", sizeof(body->subject) - 1);
    body->body_text = strdup("body");
    body->body_text_len = strlen(body->body_text);
    body->hop_count = 100;

    obj = plank_object_create_message(node_id, "routetest@testnet", body, privkey);
    plank_message_body_free(body);
    ASSERT(obj != NULL);

    /* hop_count=100, max=16: should fail */
    ok = plank_router_check_hops(test_router, obj, "general@routetest@testnet");
    ASSERT(!ok);
    plank_object_free(obj);
}

TEST(loop_detection) {
    plank_object_t *obj = create_test_message("general@routetest@testnet", "Loop Test");
    ASSERT(obj != NULL);

    uint8_t local_id[PLANK_NODE_ID_SIZE] = {0};
    plank_node_identity_t ident;
    if (plank_store_get_identity(test_store, &ident))
        memcpy(local_id, ident.node_id, PLANK_NODE_ID_SIZE);

    /* Fresh object with no path: no loop */
    bool has_loop = plank_router_check_loop(test_router, obj, local_id);
    ASSERT(!has_loop);

    plank_object_free(obj);
}

TEST(queue_outbound) {
    plank_object_t *obj = create_test_message("general@routetest@testnet", "Queue Test");
    ASSERT(obj != NULL);

    /* Store object first so queue can reference it */
    int64_t seq = 0;
    plank_store_object_put(test_store, obj, PLANK_SOURCE_LOCAL, 0, &seq);

    bool ok = plank_router_queue_outbound(test_router, obj, 0);
    ASSERT(ok);

    plank_object_free(obj);
}

TEST(retry_policy) {
    plank_retry_policy_t policy;
    plank_router_get_retry_policy(test_router, &policy);

    ASSERT(policy.initial_sec > 0);
    ASSERT(policy.max_sec >= policy.initial_sec);
    ASSERT(policy.limit > 0);

    int delay0 = plank_router_calc_retry_delay(test_router, 0);
    int delay1 = plank_router_calc_retry_delay(test_router, 1);
    int delay5 = plank_router_calc_retry_delay(test_router, 5);
    int delay100 = plank_router_calc_retry_delay(test_router, 100);

    ASSERT(delay0 > 0);
    ASSERT(delay1 >= delay0);
    ASSERT(delay5 >= delay1);
    ASSERT(delay100 <= 86400); /* never more than a day */
}

int main(void) {
    printf("PLANK Routing Tests\n");
    printf("====================\n\n");

    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }

    setup();

    RUN_TEST(router_create);
    RUN_TEST(duplicate_detection);
    RUN_TEST(hop_count_validation);
    RUN_TEST(loop_detection);
    RUN_TEST(queue_outbound);
    RUN_TEST(retry_policy);

    teardown();
    plank_shutdown();

    printf("\nAll routing tests passed!\n");
    return 0;
}
