/*
 * PLANK Object Tests
 * Tests for object encoding, decoding, signing, and verification
 */

#include "plank/plank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ============================================================================
 * OBJECT LIFECYCLE TESTS
 * ============================================================================ */

static int test_object_new_free(void) {
    plank_object_t* obj = plank_object_new();
    if (!obj) return 0;
    
    if (obj->version != 1) {
        plank_object_free(obj);
        return 0;
    }
    
    if (obj->sig_alg != PLANK_SIG_ED25519) {
        plank_object_free(obj);
        return 0;
    }
    
    plank_object_free(obj);
    return 1;
}

static int test_object_clone(void) {
    plank_object_t* obj = plank_object_new();
    if (!obj) return 0;
    
    obj->object_class = PLANK_CLASS_MESSAGE;
    obj->created_at = 1234567890;
    strncpy(obj->origin_node_addr, "test@network", sizeof(obj->origin_node_addr));
    
    plank_object_t* clone = plank_object_clone(obj);
    if (!clone) {
        plank_object_free(obj);
        return 0;
    }
    
    int result = (clone->object_class == obj->object_class &&
                  clone->created_at == obj->created_at &&
                  strcmp(clone->origin_node_addr, obj->origin_node_addr) == 0);
    
    plank_object_free(obj);
    plank_object_free(clone);
    return result;
}

/* ============================================================================
 * MESSAGE BODY TESTS
 * ============================================================================ */

static int test_message_body_new_free(void) {
    plank_message_body_t* body = plank_message_body_new();
    if (!body) return 0;
    
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->author_user, "testuser", sizeof(body->author_user));
    strncpy(body->subject, "Test Subject", sizeof(body->subject));
    body->body_text = strdup("Test body content");
    body->body_text_len = strlen(body->body_text);
    
    plank_message_body_free(body);
    return 1;
}

static int test_message_body_clone(void) {
    plank_message_body_t* body = plank_message_body_new();
    if (!body) return 0;
    
    body->message_type = PLANK_MSG_DIRECT_POST;
    strncpy(body->author_user, "sender", sizeof(body->author_user));
    strncpy(body->subject, "Hello", sizeof(body->subject));
    body->body_text = strdup("Message content");
    body->body_text_len = strlen(body->body_text);
    
    plank_message_body_t* clone = plank_message_body_clone(body);
    if (!clone) {
        plank_message_body_free(body);
        return 0;
    }
    
    int result = (clone->message_type == body->message_type &&
                  strcmp(clone->author_user, body->author_user) == 0 &&
                  strcmp(clone->subject, body->subject) == 0 &&
                  clone->body_text_len == body->body_text_len &&
                  strcmp(clone->body_text, body->body_text) == 0);
    
    plank_message_body_free(body);
    plank_message_body_free(clone);
    return result;
}

/* ============================================================================
 * OBJECT ID TESTS
 * ============================================================================ */

static int test_object_id_hex_roundtrip(void) {
    uint8_t id[PLANK_OBJECT_ID_SIZE];
    plank_crypto_random(id, sizeof(id));
    
    char hex[65];
    plank_object_id_to_hex(id, hex);
    
    if (strlen(hex) != 64) return 0;
    
    uint8_t decoded[PLANK_OBJECT_ID_SIZE];
    if (!plank_hex_to_object_id(hex, decoded)) return 0;
    
    return memcmp(id, decoded, PLANK_OBJECT_ID_SIZE) == 0;
}

static int test_object_id_equal(void) {
    uint8_t id1[PLANK_OBJECT_ID_SIZE];
    uint8_t id2[PLANK_OBJECT_ID_SIZE];
    
    plank_crypto_random(id1, sizeof(id1));
    memcpy(id2, id1, sizeof(id1));
    
    if (!plank_object_id_equal(id1, id2)) return 0;
    
    id2[0] ^= 0x01;
    if (plank_object_id_equal(id1, id2)) return 0;
    
    return 1;
}

static int test_object_id_is_zero(void) {
    uint8_t zero[PLANK_OBJECT_ID_SIZE] = {0};
    uint8_t nonzero[PLANK_OBJECT_ID_SIZE] = {0};
    nonzero[15] = 1;
    
    if (!plank_object_id_is_zero(zero)) return 0;
    if (plank_object_id_is_zero(nonzero)) return 0;
    
    return 1;
}

/* ============================================================================
 * ADDRESS TESTS
 * ============================================================================ */

static int test_format_node_addr(void) {
    char addr[PLANK_MAX_ADDRESS];
    
    if (!plank_format_node_addr("mynode", "mynetwork", addr, sizeof(addr))) {
        return 0;
    }
    
    return strcmp(addr, "mynode@mynetwork") == 0;
}

static int test_format_user_addr(void) {
    char addr[PLANK_MAX_ADDRESS];
    
    if (!plank_format_user_addr("user", "node", "network", addr, sizeof(addr))) {
        return 0;
    }
    
    return strcmp(addr, "user@node@network") == 0;
}

static int test_format_area_addr(void) {
    char addr[PLANK_MAX_ADDRESS];
    
    if (!plank_format_area_addr("general", "node", "network", addr, sizeof(addr))) {
        return 0;
    }
    
    return strcmp(addr, "general@node@network") == 0;
}

static int test_parse_node_addr(void) {
    char node[64], network[64];
    
    if (!plank_parse_node_addr("mynode@mynetwork", node, sizeof(node),
                               network, sizeof(network))) {
        return 0;
    }
    
    return strcmp(node, "mynode") == 0 && strcmp(network, "mynetwork") == 0;
}

static int test_parse_user_addr(void) {
    char user[64], node[64], network[64];
    
    if (!plank_parse_user_addr("user@node@network",
                               user, sizeof(user),
                               node, sizeof(node),
                               network, sizeof(network))) {
        return 0;
    }
    
    return strcmp(user, "user") == 0 &&
           strcmp(node, "node") == 0 &&
           strcmp(network, "network") == 0;
}

static int test_canonicalize_addr(void) {
    char addr[PLANK_MAX_ADDRESS];
    
    strncpy(addr, "MyNode@MyNetwork", sizeof(addr));
    plank_canonicalize_addr(addr);
    
    return strcmp(addr, "mynode@mynetwork") == 0;
}

static int test_validate_node_addr(void) {
    if (!plank_validate_node_addr("valid@network")) return 0;
    if (plank_validate_node_addr("invalid")) return 0;
    if (plank_validate_node_addr("@network")) return 0;
    if (plank_validate_node_addr("node@")) return 0;
    if (plank_validate_node_addr("")) return 0;
    
    return 1;
}

/* ============================================================================
 * OBJECT CLASS NAME TESTS
 * ============================================================================ */

static int test_object_class_name(void) {
    if (strcmp(plank_object_class_name(PLANK_CLASS_MESSAGE), "Message") != 0) return 0;
    if (strcmp(plank_object_class_name(PLANK_CLASS_AREA_DEFINITION), "AreaDefinition") != 0) return 0;
    if (strcmp(plank_object_class_name(PLANK_CLASS_MODERATION), "ModerationEvent") != 0) return 0;
    
    return 1;
}

static int test_message_type_name(void) {
    if (strcmp(plank_message_type_name(PLANK_MSG_AREA_POST), "AREA_POST") != 0) return 0;
    if (strcmp(plank_message_type_name(PLANK_MSG_DIRECT_POST), "DIRECT_POST") != 0) return 0;
    
    return 1;
}

/* ============================================================================
 * OBJECT CREATION AND SIGNING TESTS
 * ============================================================================ */

static int test_create_sign_verify_message(void) {
    /* Generate keys */
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    
    if (!plank_crypto_keygen_ed25519(pubkey, privkey)) {
        return 0;
    }
    
    /* Create node ID */
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create message body */
    plank_message_body_t* body = plank_message_body_new();
    if (!body) return 0;
    
    body->message_type = PLANK_MSG_AREA_POST;
    strncpy(body->author_user, "testuser", sizeof(body->author_user));
    strncpy(body->from_addr, "testuser@node@network", sizeof(body->from_addr));
    strncpy(body->area_addr, "general@node@network", sizeof(body->area_addr));
    strncpy(body->subject, "Test Subject", sizeof(body->subject));
    body->body_format = PLANK_BODY_PLAIN_UTF8;
    body->body_text = strdup("This is a test message body.");
    body->body_text_len = strlen(body->body_text);
    body->visibility = PLANK_VIS_VISIBLE;
    body->retention_class = PLANK_RETENTION_STANDARD;
    
    /* Create object */
    plank_object_t* obj = plank_object_create_message(
        node_id, "node@network", body, privkey);
    
    plank_message_body_free(body);
    
    if (!obj) {
        return 0;
    }
    
    /* Verify object has ID */
    if (plank_object_id_is_zero(obj->object_id)) {
        plank_object_free(obj);
        return 0;
    }
    
    /* Verify signature */
    if (!plank_object_verify(obj, pubkey)) {
        plank_object_free(obj);
        return 0;
    }
    
    /* Verify ID */
    if (!plank_object_verify_id(obj)) {
        plank_object_free(obj);
        return 0;
    }
    
    plank_object_free(obj);
    return 1;
}

static int test_object_encode_decode_roundtrip(void) {
    /* Generate keys */
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create message */
    plank_message_body_t* body = plank_message_body_new();
    body->message_type = PLANK_MSG_DIRECT_POST;
    strncpy(body->author_user, "sender", sizeof(body->author_user));
    strncpy(body->from_addr, "sender@node@net", sizeof(body->from_addr));
    strncpy(body->subject, "Hello", sizeof(body->subject));
    body->body_text = strdup("Test message");
    body->body_text_len = strlen(body->body_text);
    
    plank_object_t* original = plank_object_create_message(
        node_id, "node@net", body, privkey);
    plank_message_body_free(body);
    
    if (!original) return 0;
    
    /* Decode from envelope */
    plank_object_t* decoded = plank_object_decode(
        original->envelope_cbor, original->envelope_cbor_len);
    
    if (!decoded) {
        plank_object_free(original);
        return 0;
    }
    
    /* Compare */
    int result = (decoded->version == original->version &&
                  decoded->object_class == original->object_class &&
                  decoded->created_at == original->created_at &&
                  strcmp(decoded->origin_node_addr, original->origin_node_addr) == 0 &&
                  plank_object_id_equal(decoded->object_id, original->object_id));
    
    plank_object_free(original);
    plank_object_free(decoded);
    return result;
}

static int test_message_body_decode(void) {
    /* Generate keys */
    uint8_t pubkey[PLANK_PUBKEY_SIZE];
    uint8_t privkey[PLANK_PRIVKEY_SIZE];
    plank_crypto_keygen_ed25519(pubkey, privkey);
    
    uint8_t node_id[PLANK_NODE_ID_SIZE];
    plank_crypto_random_node_id(node_id);
    
    /* Create message with specific body */
    plank_message_body_t* original_body = plank_message_body_new();
    original_body->message_type = PLANK_MSG_AREA_POST;
    strncpy(original_body->author_user, "testuser", sizeof(original_body->author_user));
    strncpy(original_body->author_display, "Test User", sizeof(original_body->author_display));
    strncpy(original_body->from_addr, "testuser@node@net", sizeof(original_body->from_addr));
    strncpy(original_body->area_addr, "general@node@net", sizeof(original_body->area_addr));
    strncpy(original_body->subject, "Test Subject", sizeof(original_body->subject));
    original_body->body_format = PLANK_BODY_PLAIN_UTF8;
    original_body->body_text = strdup("This is the message body.");
    original_body->body_text_len = strlen(original_body->body_text);
    original_body->visibility = PLANK_VIS_VISIBLE;
    original_body->hop_count = 3;
    
    plank_object_t* obj = plank_object_create_message(
        node_id, "node@net", original_body, privkey);
    
    if (!obj) {
        plank_message_body_free(original_body);
        return 0;
    }
    
    /* Decode body */
    plank_message_body_t* decoded_body = plank_object_decode_message_body(obj);
    
    if (!decoded_body) {
        plank_object_free(obj);
        plank_message_body_free(original_body);
        return 0;
    }
    
    /* Compare */
    int result = (decoded_body->message_type == original_body->message_type &&
                  strcmp(decoded_body->author_user, original_body->author_user) == 0 &&
                  strcmp(decoded_body->subject, original_body->subject) == 0 &&
                  decoded_body->body_text_len == original_body->body_text_len &&
                  strcmp(decoded_body->body_text, original_body->body_text) == 0 &&
                  decoded_body->hop_count == original_body->hop_count);
    
    plank_message_body_free(decoded_body);
    plank_message_body_free(original_body);
    plank_object_free(obj);
    return result;
}

/* ============================================================================
 * OBJECT VALIDATION TESTS
 * ============================================================================ */

static int test_object_validate(void) {
    plank_object_t* obj = plank_object_new();
    char error[256];
    
    /* Empty object should fail */
    if (plank_object_validate(obj, error, sizeof(error))) {
        plank_object_free(obj);
        return 0;
    }
    
    /* Set required fields */
    obj->object_class = PLANK_CLASS_MESSAGE;
    obj->created_at = 1234567890;
    strncpy(obj->origin_node_addr, "node@network", sizeof(obj->origin_node_addr));
    
    /* Still missing body */
    if (plank_object_validate(obj, error, sizeof(error))) {
        plank_object_free(obj);
        return 0;
    }
    
    /* Add body */
    obj->body_cbor = (uint8_t*)malloc(10);
    obj->body_cbor_len = 10;
    
    /* Should pass now */
    if (!plank_object_validate(obj, error, sizeof(error))) {
        plank_object_free(obj);
        return 0;
    }
    
    plank_object_free(obj);
    return 1;
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
    
    printf("PLANK Object Tests\n");
    printf("==================\n\n");
    
    printf("Object lifecycle tests:\n");
    TEST(object_new_free);
    TEST(object_clone);
    
    printf("\nMessage body tests:\n");
    TEST(message_body_new_free);
    TEST(message_body_clone);
    
    printf("\nObject ID tests:\n");
    TEST(object_id_hex_roundtrip);
    TEST(object_id_equal);
    TEST(object_id_is_zero);
    
    printf("\nAddress tests:\n");
    TEST(format_node_addr);
    TEST(format_user_addr);
    TEST(format_area_addr);
    TEST(parse_node_addr);
    TEST(parse_user_addr);
    TEST(canonicalize_addr);
    TEST(validate_node_addr);
    
    printf("\nClass/type name tests:\n");
    TEST(object_class_name);
    TEST(message_type_name);
    
    printf("\nObject creation and signing tests:\n");
    TEST(create_sign_verify_message);
    TEST(object_encode_decode_roundtrip);
    TEST(message_body_decode);
    
    printf("\nValidation tests:\n");
    TEST(object_validate);
    
    printf("\n==================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    plank_shutdown();
    return tests_passed == tests_run ? 0 : 1;
}
