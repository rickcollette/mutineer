/*
 * PLANK Store Tests
 * Tests for database operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "plank/plank.h"
#include "plank/plank_store.h"
#include "plank/plank_object.h"
#include "plank/plank_crypto.h"
#include "plank/plank_types.h"
#include "bbs_db.h"

static char test_db_path[256];
static BbsDb* test_db = NULL;
static plank_store_t* test_store = NULL;

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

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL\n    Expected \"%s\" == \"%s\"\n    at %s:%d\n", \
               (a), (b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

static void setup(void) {
    snprintf(test_db_path, sizeof(test_db_path), "/tmp/plank_store_test_%d.db", getpid());

    test_db = db_open(test_db_path);
    ASSERT(test_db != NULL);

    test_store = plank_store_open(test_db);
    ASSERT(test_store != NULL);

    /* Initialize PLANK schema tables (identity, peers, objects, etc.) */
    bool ok = plank_store_init_schema(test_store, "sql/plank_schema.sql");
    if (!ok) {
        /* Try relative to project root */
        ok = plank_store_init_schema(test_store, "../sql/plank_schema.sql");
    }
    ASSERT(ok);

    /* Disable FK enforcement so tests don't need full graph of linked records */
    db_exec(test_db, "PRAGMA foreign_keys=OFF");
}

static void teardown(void) {
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

/* Helper: create a signed test message object using the modern API. */
static plank_object_t* make_test_message(const char* subject, const char* body_text) {
    uint8_t pubkey[32], privkey[32];
    bool ok = plank_crypto_keygen_ed25519(pubkey, privkey);
    ASSERT(ok);

    plank_message_body_t* body = plank_message_body_new();
    ASSERT(body != NULL);
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->area_addr, "general@testnode@testnet", sizeof(body->area_addr) - 1);
    strncpy(body->subject, subject, sizeof(body->subject) - 1);
    strncpy(body->author_user, "testuser", sizeof(body->author_user) - 1);
    strncpy(body->author_display, "Test Author", sizeof(body->author_display) - 1);
    body->body_text = strdup(body_text);
    body->body_text_len = strlen(body->body_text);

    uint8_t node_id[PLANK_NODE_ID_SIZE] = {0};
    plank_object_t* obj = plank_object_create_message(
        node_id, "testnode@testnet", body, privkey);
    plank_message_body_free(body);
    return obj;
}

TEST(store_open_close) {
    ASSERT(test_store != NULL);
}

TEST(generate_identity) {
    bool ok = plank_store_generate_identity(test_store, "testnode", "testnet");
    ASSERT(ok);

    plank_node_identity_t identity;
    memset(&identity, 0, sizeof(identity));

    ok = plank_store_get_identity(test_store, &identity);
    ASSERT(ok);

    ASSERT_STR_EQ(identity.node_name, "testnode");
    ASSERT_STR_EQ(identity.network_name, "testnet");
    ASSERT(identity.node_id[0] != 0 || identity.node_id[1] != 0);

    uint8_t zero_key[32] = {0};
    ASSERT(memcmp(identity.signing_key_pub, zero_key, 32) != 0);
    ASSERT(memcmp(identity.signing_key_priv, zero_key, 32) != 0);
}

TEST(config_get_set) {
    /* Default value returned when key absent */
    int val = plank_store_config_get_int(test_store, "test_key", 42);
    ASSERT_EQ(val, 42);

    /* Set via string API */
    bool ok = plank_store_config_set(test_store, "test_key", "123");
    ASSERT(ok);

    val = plank_store_config_get_int(test_store, "test_key", 0);
    ASSERT_EQ(val, 123);
}

TEST(object_put_exists) {
    plank_object_t* obj = make_test_message("Test Subject", "Test body text");
    ASSERT(obj != NULL);
    ASSERT(obj->envelope_cbor != NULL);
    ASSERT(obj->envelope_cbor_len > 0);

    int64_t local_seq = 0;
    bool ok = plank_store_object_put(test_store, obj, PLANK_SOURCE_LOCAL, 0, &local_seq);
    ASSERT(ok);
    ASSERT(local_seq > 0);

    ok = plank_store_object_exists(test_store, obj->object_id);
    ASSERT(ok);

    uint8_t fake_id[PLANK_OBJECT_ID_SIZE];
    memset(fake_id, 0xAB, sizeof(fake_id));
    ok = plank_store_object_exists(test_store, fake_id);
    ASSERT(!ok);

    plank_object_free(obj);
}

TEST(dedupe_check) {
    uint8_t obj_id[PLANK_OBJECT_ID_SIZE];
    plank_crypto_random(obj_id, sizeof(obj_id));

    bool is_dup = plank_store_dedupe_exists(test_store, obj_id);
    ASSERT(!is_dup);

    bool ok = plank_store_dedupe_record(test_store, obj_id);
    ASSERT(ok);

    is_dup = plank_store_dedupe_exists(test_store, obj_id);
    ASSERT(is_dup);
}

TEST(transaction_commit) {
    bool ok = plank_store_begin(test_store);
    ASSERT(ok);

    ok = plank_store_config_set(test_store, "txn_test", "999");
    ASSERT(ok);

    ok = plank_store_commit(test_store);
    ASSERT(ok);

    int val = plank_store_config_get_int(test_store, "txn_test", 0);
    ASSERT_EQ(val, 999);
}

TEST(transaction_rollback) {
    bool ok = plank_store_config_set(test_store, "rollback_test", "100");
    ASSERT(ok);

    ok = plank_store_begin(test_store);
    ASSERT(ok);

    ok = plank_store_config_set(test_store, "rollback_test", "200");
    ASSERT(ok);

    ok = plank_store_rollback(test_store);
    ASSERT(ok);

    int val = plank_store_config_get_int(test_store, "rollback_test", 0);
    ASSERT_EQ(val, 100);
}

TEST(deadletter_operations) {
    uint8_t bundle_id[16];
    plank_crypto_random(bundle_id, 16);

    uint8_t obj_id[PLANK_OBJECT_ID_SIZE];
    plank_crypto_random(obj_id, sizeof(obj_id));
    const uint8_t* ids[] = { obj_id };

    bool ok = plank_store_deadletter_add(test_store,
                                         1, "remotenode@testnet",
                                         ids, 1,
                                         bundle_id,
                                         42, "Test failure reason");
    ASSERT(ok);
}

TEST(quarantine_operations) {
    plank_object_t* obj = make_test_message("Quarantine Test", "Quarantine body");
    ASSERT(obj != NULL);

    bool ok = plank_store_quarantine_add(test_store,
                                          obj->object_id,
                                          PLANK_CLASS_MESSAGE,
                                          1, "badnode@testnet",
                                          PLANK_QUARANTINE_POLICY_DENY,
                                          "Policy violation",
                                          obj->envelope_cbor,
                                          obj->envelope_cbor_len);
    ASSERT(ok);

    plank_object_free(obj);
}

TEST(audit_log) {
    bool ok = plank_store_audit_log(test_store, "test", 0, "localhost", NULL, NULL, "Test audit entry");
    ASSERT(ok);

    ok = plank_store_audit_log(test_store, "import", 1, "remotenode@testnet", NULL, NULL, "Imported bundle xyz");
    ASSERT(ok);
}

int main(void) {
    printf("PLANK Store Tests\n");
    printf("==================\n\n");

    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }

    setup();

    RUN_TEST(store_open_close);
    RUN_TEST(generate_identity);
    RUN_TEST(config_get_set);
    RUN_TEST(object_put_exists);
    RUN_TEST(dedupe_check);
    RUN_TEST(transaction_commit);
    RUN_TEST(transaction_rollback);
    RUN_TEST(deadletter_operations);
    RUN_TEST(quarantine_operations);
    RUN_TEST(audit_log);

    teardown();
    plank_shutdown();

    printf("\nAll store tests passed!\n");
    return 0;
}
