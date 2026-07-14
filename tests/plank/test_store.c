/*
 * PLANK Store Tests
 * Tests for database operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

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
    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
    snprintf(test_db_path, sizeof(test_db_path), "%s/plank_store_test_XXXXXX", tmpdir);
    int fd = mkstemp(test_db_path);
    ASSERT(fd >= 0);
    close(fd);

    test_db = db_open(test_db_path);
    ASSERT(test_db != NULL);

    test_store = plank_store_open(test_db);
    ASSERT(test_store != NULL);

    bool ok = db_init_schema(test_db, "sql/schema.sql");
    if (!ok) {
        ok = db_init_schema(test_db, "../sql/schema.sql");
    }
    ASSERT(ok);

    /* Initialize PLANK schema tables (identity, peers, objects, etc.) */
    ok = plank_store_init_schema(test_store, "sql/plank_schema.sql");
    if (!ok) {
        /* Try relative to project root */
        ok = plank_store_init_schema(test_store, "../sql/plank_schema.sql");
    }
    ASSERT(ok);

    int fk = db_query_int(test_db, "PRAGMA foreign_keys", 0);
    ASSERT_EQ(fk, 1);
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
    uint8_t pubkey[PLANK_PUBKEY_SIZE], privkey[PLANK_PRIVKEY_SIZE];
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

static int seed_test_link(void) {
    static int seed_counter = 0;
    seed_counter++;
    plank_peer_t peer;
    memset(&peer, 0, sizeof(peer));
    ASSERT(plank_crypto_random(peer.node_id, sizeof(peer.node_id)));
    ASSERT(plank_crypto_random(peer.signing_key_pub, sizeof(peer.signing_key_pub)));
    snprintf(peer.node_name, sizeof(peer.node_name), "remote'node%d", seed_counter);
    strncpy(peer.network_name, "test'net", sizeof(peer.network_name) - 1);
    snprintf(peer.node_addr, sizeof(peer.node_addr), "remote'node%d@test'net", seed_counter);
    strncpy(peer.tls_fingerprint, "fp'123", sizeof(peer.tls_fingerprint) - 1);
    strncpy(peer.notes, "peer note with ' quote", sizeof(peer.notes) - 1);
    peer.trust_level = 10;
    peer.status = 1;
    ASSERT(plank_store_peer_upsert(test_store, &peer));

    int peer_id = db_query_int(test_db, "SELECT id FROM plank_peers ORDER BY id DESC LIMIT 1", 0);
    ASSERT(peer_id > 0);

    plank_link_t link;
    memset(&link, 0, sizeof(link));
    ASSERT(plank_crypto_random(link.link_id, sizeof(link.link_id)));
    snprintf(link.link_name, sizeof(link.link_name), "link'one%d", seed_counter);
    link.peer_id = peer_id;
    strncpy(link.remote_host, "host'example", sizeof(link.remote_host) - 1);
    link.remote_port = 24554;
    link.direction = PLANK_DIR_BOTH;
    link.enabled = true;
    link.paused = false;
    link.retry_initial_sec = 5;
    link.retry_max_sec = 60;
    link.retry_limit = 3;
    link.state = PLANK_LINK_IDLE;

    int link_id = 0;
    ASSERT(plank_store_link_add(test_store, &link, &link_id));
    ASSERT(link_id > 0);
    ASSERT(plank_store_link_set_error(test_store, link_id, "link error with ' quote"));
    return link_id;
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

    ok = plank_store_config_set(test_store, "quote'key", "456");
    ASSERT(ok);
    val = plank_store_config_get_int(test_store, "quote'key", 0);
    ASSERT_EQ(val, 456);
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
    int link_id = seed_test_link();
    uint8_t bundle_id[16];
    plank_crypto_random(bundle_id, 16);

    uint8_t obj_id[PLANK_OBJECT_ID_SIZE];
    plank_crypto_random(obj_id, sizeof(obj_id));
    const uint8_t* ids[] = { obj_id };

    bool ok = plank_store_deadletter_add(test_store,
                                         link_id, "remote'node@testnet",
                                         ids, 1,
                                         bundle_id,
                                         42, "Test failure ' reason");
    ASSERT(ok);
}

TEST(deadletter_100_objects) {
    int link_id = seed_test_link();
    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    plank_crypto_random(bundle_id, sizeof(bundle_id));

    uint8_t obj_ids[100][PLANK_OBJECT_ID_SIZE];
    const uint8_t* ids[100];
    for (int i = 0; i < 100; i++) {
        ASSERT(plank_crypto_random(obj_ids[i], sizeof(obj_ids[i])));
        ids[i] = obj_ids[i];
    }

    bool ok = plank_store_deadletter_add(test_store,
                                         link_id, "remote100@testnet",
                                         ids, 100,
                                         bundle_id,
                                         9001, "large object list");
    ASSERT(ok);

    uint8_t extra_id[PLANK_OBJECT_ID_SIZE];
    ASSERT(plank_crypto_random(extra_id, sizeof(extra_id)));
    const uint8_t* too_many[101];
    for (int i = 0; i < 100; i++) too_many[i] = ids[i];
    too_many[100] = extra_id;
    ok = plank_store_deadletter_add(test_store,
                                    link_id, "remote101@testnet",
                                    too_many, 101,
                                    bundle_id,
                                    9002, "too many");
    ASSERT(!ok);
}

TEST(quarantine_operations) {
    int link_id = seed_test_link();
    plank_object_t* obj = make_test_message("Quarantine Test", "Quarantine body");
    ASSERT(obj != NULL);

    bool ok = plank_store_quarantine_add(test_store,
                                          obj->object_id,
                                          PLANK_CLASS_MESSAGE,
                                          link_id, "bad'node@testnet",
                                          PLANK_QUARANTINE_POLICY_DENY,
                                          "Policy ' violation",
                                          obj->envelope_cbor,
                                          obj->envelope_cbor_len);
    ASSERT(ok);

    plank_object_free(obj);
}

TEST(quoted_area_import_and_object_values) {
    plank_area_t area;
    memset(&area, 0, sizeof(area));
    strncpy(area.area_addr, "general'area@testnet", sizeof(area.area_addr) - 1);
    strncpy(area.area_slug, "general-quote", sizeof(area.area_slug) - 1);
    strncpy(area.origin_node_addr, "origin'node@testnet", sizeof(area.origin_node_addr) - 1);
    strncpy(area.title, "Title with ' quote", sizeof(area.title) - 1);
    strncpy(area.description, "Description with ' quote", sizeof(area.description) - 1);
    area.max_message_bytes = 1024 * 1024;
    area.max_attachment_bytes = 1024 * 1024;
    area.status = 1;
    int area_id = 0;
    if (!plank_store_area_upsert(test_store, &area, &area_id)) {
        fprintf(stderr, "area upsert failed: %s\n", db_last_error(test_db));
        ASSERT(0);
    }

    plank_object_t* obj = make_test_message("Object Quote Test", "Object body");
    ASSERT(obj != NULL);
    strncpy(obj->origin_node_addr, "origin'object@testnet", sizeof(obj->origin_node_addr) - 1);
    int64_t local_seq = 0;
    ASSERT(plank_store_object_put(test_store, obj, PLANK_SOURCE_LOCAL, 0, &local_seq));
    ASSERT(local_seq > 0);

    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    uint8_t source_node_id[PLANK_NODE_ID_SIZE];
    ASSERT(plank_crypto_random(bundle_id, sizeof(bundle_id)));
    ASSERT(plank_crypto_random(source_node_id, sizeof(source_node_id)));
    ASSERT(plank_store_import_record(test_store, bundle_id, PLANK_BUNDLE_LINK_SYNC,
                                     source_node_id, "source'node@testnet", 0,
                                     PLANK_IMPORT_ACCEPTED, 1, 0, 0));

    plank_object_free(obj);
}

TEST(audit_log) {
    bool ok = plank_store_audit_log(test_store, "test", 0, "localhost", NULL, NULL, "Test audit entry");
    ASSERT(ok);

    int link_id = seed_test_link();
    uint8_t obj_id[PLANK_OBJECT_ID_SIZE];
    ASSERT(plank_crypto_random(obj_id, sizeof(obj_id)));
    ok = plank_store_audit_log(test_store, "import'event", link_id, "remote'node@testnet", "sys'op", obj_id, "Imported bundle ' xyz");
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
    RUN_TEST(deadletter_100_objects);
    RUN_TEST(quarantine_operations);
    RUN_TEST(quoted_area_import_and_object_values);
    RUN_TEST(audit_log);

    teardown();
    plank_shutdown();

    printf("\nAll store tests passed!\n");
    return 0;
}
