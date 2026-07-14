/*
 * PLANK Bundle Tests
 * Tests for bundle creation, reading, and import/export
 */

#include "plank/plank.h"
#include "bbs_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %s... ", #name); \
    tests_run++; \
    if (test_##name()) { \
        printf("PASS\n"); \
        tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

/* Test temp directory */
static char g_temp_dir[256];

static void setup_temp_dir(void) {
    snprintf(g_temp_dir, sizeof(g_temp_dir), "%s/plank_test_%d",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    mkdir(g_temp_dir, 0755);
}

static void cleanup_temp_dir(void) {
    bbs_remove_tree(g_temp_dir);
}

/* ============================================================================
 * BUNDLE WRITER TESTS
 * ============================================================================ */

static int test_bundle_writer_create(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test1.plb", g_temp_dir);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    if (!writer) return 0;
    
    plank_bundle_writer_close(writer);
    return 1;
}

static int test_bundle_writer_finalize_empty(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_empty.plb", g_temp_dir);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    if (!writer) return 0;
    
    if (!plank_bundle_writer_finalize(writer, NULL)) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    uint8_t bundle_id[PLANK_BUNDLE_ID_SIZE];
    plank_bundle_writer_get_id(writer, bundle_id);
    
    /* Verify bundle ID is not zero */
    int nonzero = 0;
    for (int i = 0; i < PLANK_BUNDLE_ID_SIZE; i++) {
        if (bundle_id[i] != 0) nonzero = 1;
    }
    
    plank_bundle_writer_close(writer);
    
    /* Verify file exists */
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    
    return nonzero;
}

static int test_bundle_writer_add_object(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_obj.plb", g_temp_dir);
    
    /* Generate keys */
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create bundle */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    if (!writer) return 0;
    
    /* Create a message object */
    plank_message_body_t* body = plank_message_body_new();
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->author_user, "testuser", sizeof(body->author_user));
    strncpy(body->from_addr, "testuser@test@network", sizeof(body->from_addr));
    strncpy(body->area_addr, "general@test@network", sizeof(body->area_addr));
    strncpy(body->subject, "Test", sizeof(body->subject));
    body->body_text = strdup("Test message");
    body->body_text_len = strlen(body->body_text);
    
    plank_object_t* obj = plank_object_create_message(
        node_id, "test@network", body, privkey);
    plank_message_body_free(body);
    
    if (!obj) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    /* Add to bundle */
    if (!plank_bundle_writer_add_object(writer, obj)) {
        plank_object_free(obj);
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    plank_object_free(obj);
    
    /* Finalize */
    if (!plank_bundle_writer_finalize(writer, privkey)) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    plank_bundle_writer_close(writer);
    return 1;
}

static int test_bundle_writer_add_attachment(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_att.plb", g_temp_dir);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    if (!writer) return 0;
    
    /* Create attachment data */
    uint8_t data[1024];
    for (int i = 0; i < 1024; i++) {
        data[i] = (uint8_t)(i % 256);
    }
    
    uint8_t att_id[PLANK_ATTACHMENT_ID_SIZE];
    plank_crypto_sha256(data, sizeof(data), att_id);
    
    if (!plank_bundle_writer_add_attachment(writer, att_id, data, sizeof(data), false)) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    if (!plank_bundle_writer_finalize(writer, NULL)) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    plank_bundle_writer_close(writer);
    return 1;
}

/* ============================================================================
 * BUNDLE READER TESTS
 * ============================================================================ */

static int test_bundle_reader_open(void) {
    /* First create a bundle */
    char path[512];
    snprintf(path, sizeof(path), "%s/test_read.plb", g_temp_dir);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    if (!writer) return 0;
    
    plank_bundle_writer_finalize(writer, NULL);
    plank_bundle_writer_close(writer);
    
    /* Now read it */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    const plank_bundle_manifest_t* manifest = plank_bundle_reader_manifest(reader);
    if (!manifest) {
        plank_bundle_reader_close(reader);
        return 0;
    }
    
    int result = (manifest->bundle_type == PLANK_BUNDLE_LINK_SYNC);
    
    plank_bundle_reader_close(reader);
    return result;
}

static int test_bundle_reader_manifest(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_manifest.plb", g_temp_dir);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    uint8_t link_id[PLANK_LINK_ID_SIZE];
    plank_crypto_random_link_id(link_id);
    
    /* Create bundle with specific manifest data */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_USER_EXPORT, node_id, "mynode@mynetwork");
    if (!writer) return 0;
    
    plank_bundle_writer_add_checkpoint(writer, link_id, PLANK_DIR_OUTBOUND, 12345, 12345);
    plank_bundle_writer_finalize(writer, NULL);
    
    uint8_t expected_bundle_id[PLANK_BUNDLE_ID_SIZE];
    plank_bundle_writer_get_id(writer, expected_bundle_id);
    plank_bundle_writer_close(writer);
    
    /* Read and verify */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    const plank_bundle_manifest_t* manifest = plank_bundle_reader_manifest(reader);
    
    int result = (manifest->bundle_type == PLANK_BUNDLE_USER_EXPORT &&
                  strcmp(manifest->source_node_addr, "mynode@mynetwork") == 0 &&
                  memcmp(manifest->bundle_id, expected_bundle_id, PLANK_BUNDLE_ID_SIZE) == 0);
    
    plank_bundle_reader_close(reader);
    return result;
}

static int test_bundle_reader_entries(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_entries.plb", g_temp_dir);
    
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create bundle with multiple entries */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    if (!writer) return 0;
    
    /* Add 3 objects */
    for (int i = 0; i < 3; i++) {
        plank_message_body_t* body = plank_message_body_new();
        body->message_type = PLANK_MSG_AREA_POST;
        snprintf(body->author_user, sizeof(body->author_user), "user%d", i);
        snprintf(body->subject, sizeof(body->subject), "Message %d", i);
        body->body_text = strdup("Content");
        body->body_text_len = strlen(body->body_text);
        
        plank_object_t* obj = plank_object_create_message(
            node_id, "test@network", body, privkey);
        plank_message_body_free(body);
        
        if (obj) {
            plank_bundle_writer_add_object(writer, obj);
            plank_object_free(obj);
        }
    }
    
    /* Add 2 attachments */
    for (int i = 0; i < 2; i++) {
        uint8_t data[100];
        memset(data, i, sizeof(data));
        
        uint8_t att_id[PLANK_ATTACHMENT_ID_SIZE];
        plank_crypto_sha256(data, sizeof(data), att_id);
        
        plank_bundle_writer_add_attachment(writer, att_id, data, sizeof(data), false);
    }
    
    plank_bundle_writer_finalize(writer, NULL);
    plank_bundle_writer_close(writer);
    
    /* Read and verify */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    uint32_t record_count = plank_bundle_reader_record_count(reader);
    if (record_count != 5) {
        plank_bundle_reader_close(reader);
        return 0;
    }
    
    /* Count record types */
    int obj_count = 0, att_count = 0;
    for (uint32_t i = 0; i < record_count; i++) {
        plank_bundle_record_t record;
        if (plank_bundle_reader_get_record(reader, i, &record)) {
            if (record.record_type == PLANK_RECORD_OBJECT) obj_count++;
            if (record.record_type == PLANK_RECORD_ATTACHMENT) att_count++;
        }
    }
    
    plank_bundle_reader_close(reader);
    return obj_count == 3 && att_count == 2;
}

static int test_bundle_reader_load_payload(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_payload.plb", g_temp_dir);
    
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create bundle with one object */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    if (!writer) return 0;
    
    plank_message_body_t* body = plank_message_body_new();
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->author_user, "testuser", sizeof(body->author_user));
    strncpy(body->subject, "Test Subject", sizeof(body->subject));
    body->body_text = strdup("Test body content for payload test");
    body->body_text_len = strlen(body->body_text);
    
    plank_object_t* original = plank_object_create_message(
        node_id, "test@network", body, privkey);
    plank_message_body_free(body);
    
    if (!original) {
        plank_bundle_writer_close(writer);
        return 0;
    }
    
    uint8_t original_id[PLANK_OBJECT_ID_SIZE];
    memcpy(original_id, original->object_id, PLANK_OBJECT_ID_SIZE);
    
    plank_bundle_writer_add_object(writer, original);
    plank_object_free(original);
    
    plank_bundle_writer_finalize(writer, NULL);
    plank_bundle_writer_close(writer);
    
    /* Read and load payload */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    uint8_t* payload = NULL;
    size_t payload_len = 0;
    
    if (!plank_bundle_reader_load_payload(reader, 0, &payload, &payload_len)) {
        plank_bundle_reader_close(reader);
        return 0;
    }
    
    /* Decode and verify */
    plank_object_t* decoded = plank_object_decode(payload, payload_len);
    plank_bundle_reader_free_payload(payload);
    
    if (!decoded) {
        plank_bundle_reader_close(reader);
        return 0;
    }
    
    int result = plank_object_id_equal(decoded->object_id, original_id);
    
    plank_object_free(decoded);
    plank_bundle_reader_close(reader);
    return result;
}

/* ============================================================================
 * BUNDLE ROUNDTRIP TESTS
 * ============================================================================ */

static int test_bundle_roundtrip(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_roundtrip.plb", g_temp_dir);
    
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create multiple objects */
    plank_object_t* objects[5];
    for (int i = 0; i < 5; i++) {
        plank_message_body_t* body = plank_message_body_new();
        body->message_type = PLANK_MSG_AREA_POST;
        snprintf(body->author_user, sizeof(body->author_user), "user%d", i);
        snprintf(body->subject, sizeof(body->subject), "Subject %d", i);
        body->body_text = strdup("Message body content");
        body->body_text_len = strlen(body->body_text);
        
        objects[i] = plank_object_create_message(node_id, "test@network", body, privkey);
        plank_message_body_free(body);
        
        if (!objects[i]) {
            for (int j = 0; j < i; j++) plank_object_free(objects[j]);
            return 0;
        }
    }
    
    /* Write bundle */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    for (int i = 0; i < 5; i++) {
        plank_bundle_writer_add_object(writer, objects[i]);
    }
    
    plank_bundle_writer_finalize(writer, privkey);
    plank_bundle_writer_close(writer);
    
    /* Read bundle and verify all objects */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) {
        for (int i = 0; i < 5; i++) plank_object_free(objects[i]);
        return 0;
    }
    
    int verified = 0;
    for (uint32_t i = 0; i < plank_bundle_reader_record_count(reader); i++) {
        uint8_t* payload = NULL;
        size_t payload_len = 0;
        
        if (!plank_bundle_reader_load_payload(reader, i, &payload, &payload_len)) {
            continue;
        }
        
        plank_object_t* decoded = plank_object_decode(payload, payload_len);
        plank_bundle_reader_free_payload(payload);
        
        if (!decoded) continue;
        
        /* Find matching original */
        for (int j = 0; j < 5; j++) {
            if (plank_object_id_equal(decoded->object_id, objects[j]->object_id)) {
                /* Verify signature */
                if (plank_object_verify(decoded, pubkey)) {
                    verified++;
                }
                break;
            }
        }
        
        plank_object_free(decoded);
    }
    
    plank_bundle_reader_close(reader);
    for (int i = 0; i < 5; i++) plank_object_free(objects[i]);
    
    return verified == 5;
}

/* ============================================================================
 * BUNDLE VERIFICATION TESTS
 * ============================================================================ */

static int test_bundle_verify_signature(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_verify.plb", g_temp_dir);
    
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create signed bundle */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    plank_bundle_writer_finalize(writer, privkey);
    plank_bundle_writer_close(writer);
    
    /* Verify with correct key */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    int result = plank_bundle_reader_verify(reader, pubkey);
    
    plank_bundle_reader_close(reader);
    return result;
}

static int test_bundle_verify_wrong_key(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/test_wrong_key.plb", g_temp_dir);
    
    uint8_t pubkey1[PLANK_PUBKEY_SIZE], pubkey2[PLANK_PUBKEY_SIZE];
    uint8_t privkey1[PLANK_PRIVKEY_SIZE], privkey2[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey1, privkey1);
    plank_crypto_keygen_ed25519(pubkey2, privkey2);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create bundle signed with key 1 */
    plank_bundle_writer_t* writer = plank_bundle_writer_create(
        path, PLANK_BUNDLE_LINK_SYNC, node_id, "test@network");
    
    plank_bundle_writer_finalize(writer, privkey1);
    plank_bundle_writer_close(writer);
    
    /* Verify with key 2 should fail */
    plank_bundle_reader_t* reader = plank_bundle_reader_open(path);
    if (!reader) return 0;
    
    int result = !plank_bundle_reader_verify(reader, pubkey2);
    
    plank_bundle_reader_close(reader);
    return result;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    /* Initialize PLANK */
    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }
    
    setup_temp_dir();
    
    printf("PLANK Bundle Tests\n");
    printf("==================\n\n");
    
    printf("Bundle writer tests:\n");
    TEST(bundle_writer_create);
    TEST(bundle_writer_finalize_empty);
    TEST(bundle_writer_add_object);
    TEST(bundle_writer_add_attachment);
    
    printf("\nBundle reader tests:\n");
    TEST(bundle_reader_open);
    TEST(bundle_reader_manifest);
    TEST(bundle_reader_entries);
    TEST(bundle_reader_load_payload);
    
    printf("\nBundle roundtrip tests:\n");
    TEST(bundle_roundtrip);
    
    printf("\nBundle verification tests:\n");
    TEST(bundle_verify_signature);
    TEST(bundle_verify_wrong_key);
    
    printf("\n==================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    cleanup_temp_dir();
    plank_shutdown();
    
    return tests_passed == tests_run ? 0 : 1;
}
