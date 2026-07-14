/*
 * PLANK Policy Tests
 * Tests for validation and policy enforcement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "plank/plank.h"
#include "plank/plank_policy.h"
#include "plank/plank_store.h"
#include "plank/plank_object.h"
#include "plank/plank_crypto.h"
#include "plank/plank_types.h"
#include "bbs_db.h"

static char test_db_path[256];
static BbsDb *test_db = NULL;
static plank_store_t *test_store = NULL;
static plank_policy_t *test_policy = NULL;

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
    snprintf(test_db_path, sizeof(test_db_path), "%s/plank_policy_test_%d.db",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());

    test_db = db_open(test_db_path);
    ASSERT(test_db != NULL);

    test_store = plank_store_open(test_db);
    ASSERT(test_store != NULL);

    bool ok = plank_store_init_schema(test_store, "sql/plank_schema.sql");
    ASSERT(ok);
    db_exec(test_db, "PRAGMA foreign_keys=OFF");

    plank_store_generate_identity(test_store, "policytest", "testnet");

    test_policy = plank_policy_create(test_store);
    ASSERT(test_policy != NULL);
}

static void teardown(void) {
    if (test_policy) {
        plank_policy_free(test_policy);
        test_policy = NULL;
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

/* Helper: create a properly signed message object */
static plank_object_t *create_signed_message(uint8_t *pubkey_out, uint8_t *privkey_out,
                                              uint32_t hop_count) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE], privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    if (pubkey_out) memcpy(pubkey_out, pubkey, PLANK_PUBKEY_SIZE);
    if (privkey_out) memcpy(privkey_out, privkey, PLANK_PRIVKEY_SIZE);

    plank_message_body_t *body = plank_message_body_new();
    ASSERT(body != NULL);
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->area_addr, "general@policytest@testnet", sizeof(body->area_addr) - 1);
    strncpy(body->subject, "Policy Test Message", sizeof(body->subject) - 1);
    strncpy(body->author_user, "testuser", sizeof(body->author_user) - 1);
    body->body_text = strdup("Policy test body.");
    body->body_text_len = strlen(body->body_text);
    body->hop_count = hop_count;

    uint8_t node_id[PLANK_NODE_ID_SIZE] = {0};
    plank_object_t *obj = plank_object_create_message(
        node_id, "policytest@testnet", body, privkey);
    plank_message_body_free(body);
    return obj;
}

TEST(policy_create) {
    ASSERT(test_policy != NULL);
}

TEST(validate_valid_object) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    plank_object_t *obj = create_signed_message(pubkey, NULL, 0);
    ASSERT(obj != NULL);

    plank_validation_result_t result;
    memset(&result, 0, sizeof(result));

    bool ok = plank_policy_validate_object(test_policy, obj, pubkey, &result);
    ASSERT(ok);
    ASSERT(result.valid);
    ASSERT(!result.should_quarantine);

    plank_object_free(obj);
}

TEST(validate_bad_signature) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    plank_object_t *obj = create_signed_message(pubkey, NULL, 0);
    ASSERT(obj != NULL);

    /* Use a random wrong key — function returns false and sets result->valid=false */
    uint8_t wrong_key[32];
    plank_crypto_random(wrong_key, 32);

    plank_validation_result_t result;
    memset(&result, 0, sizeof(result));

    /* Return value false is expected when validation detects an error */
    plank_policy_validate_object(test_policy, obj, wrong_key, &result);
    ASSERT(!result.valid);
    ASSERT(result.should_quarantine);

    plank_object_free(obj);
}

TEST(validate_tampered_object) {
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    plank_object_t *obj = create_signed_message(pubkey, NULL, 0);
    ASSERT(obj != NULL);

    /* Corrupt the object ID */
    obj->object_id[0] ^= 0xFF;

    plank_validation_result_t result;
    memset(&result, 0, sizeof(result));

    plank_policy_validate_object(test_policy, obj, pubkey, &result);
    ASSERT(!result.valid);

    plank_object_free(obj);
}

TEST(validate_hop_count) {
    /* check_hop_count helper directly validates hop count against policy */
    bool ok = plank_policy_check_hop_count(test_policy, "general@policytest@testnet", 3);
    ASSERT(ok);  /* 3 hops, limit=16 */

    ok = plank_policy_check_hop_count(test_policy, "general@policytest@testnet", 200);
    ASSERT(!ok); /* 200 hops, limit=16 */

    ok = plank_policy_check_hop_count(test_policy, "general@policytest@testnet", 0);
    ASSERT(ok);  /* 0 hops always ok */
}

TEST(can_post_check) {
    /* Non-banned user, can_post should be true (no bans configured) */
    bool ok = plank_policy_can_post(test_policy,
                                    "general@policytest@testnet",
                                    "testuser@policytest@testnet");
    ASSERT(ok);
}

TEST(ban_checks) {
    /* Unknown node/user: not banned */
    bool banned = plank_policy_is_node_banned(test_policy, "unknown@testnet");
    ASSERT(!banned);

    banned = plank_policy_is_user_banned(test_policy, "nobody@unknown@testnet");
    ASSERT(!banned);
}

int main(void) {
    printf("PLANK Policy Tests\n");
    printf("===================\n\n");

    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }

    setup();

    RUN_TEST(policy_create);
    RUN_TEST(validate_valid_object);
    RUN_TEST(validate_bad_signature);
    RUN_TEST(validate_tampered_object);
    RUN_TEST(validate_hop_count);
    RUN_TEST(can_post_check);
    RUN_TEST(ban_checks);

    teardown();
    plank_shutdown();

    printf("\nAll policy tests passed!\n");
    return 0;
}
