/*
 * PLANK Link/Wire Tests
 * Tests for wire protocol structures and link session management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "plank/plank.h"
#include "plank/plank_wire.h"
#include "plank/plank_link.h"
#include "plank/plank_crypto.h"
#include "plank/plank_types.h"

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

TEST(frame_header_size) {
    ASSERT_EQ((int)sizeof(plank_frame_hdr_t), PLANK_FRAME_HEADER_SIZE);
    ASSERT_EQ(PLANK_FRAME_HEADER_SIZE, 24);
}

TEST(frame_header_layout) {
    plank_frame_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic[0] = 'P';
    hdr.magic[1] = 'L';
    hdr.magic[2] = 'K';
    hdr.magic[3] = '1';
    hdr.frame_type = plank_htons(PLANK_FRAME_HELLO);
    hdr.flags = plank_htons(0);
    hdr.payload_len = plank_htonl(100);
    hdr.correlation_id = plank_htonll(12345);
    hdr.reserved = 0;

    ASSERT(hdr.magic[0] == 'P');
    ASSERT(hdr.magic[1] == 'L');
    ASSERT(hdr.magic[2] == 'K');
    ASSERT(hdr.magic[3] == '1');

    uint16_t frame_type = plank_ntohs(hdr.frame_type);
    ASSERT_EQ(frame_type, PLANK_FRAME_HELLO);

    uint32_t payload_len = plank_ntohl(hdr.payload_len);
    ASSERT_EQ(payload_len, 100);

    uint64_t corr_id = plank_ntohll(hdr.correlation_id);
    ASSERT_EQ(corr_id, 12345);
}

TEST(bundle_header_size) {
    ASSERT_EQ((int)sizeof(plank_bundle_hdr_t), PLANK_BUNDLE_HEADER_SIZE);
    ASSERT_EQ(PLANK_BUNDLE_HEADER_SIZE, 32);
}

TEST(bundle_header_layout) {
    plank_bundle_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* plank_bundle_hdr_init: type, flags, manifest_len, dir_count (5 args) */
    plank_bundle_hdr_init(&hdr, PLANK_BUNDLE_LINK_SYNC, 0, 0, 5);

    ASSERT(plank_bundle_hdr_valid_magic(&hdr));
    ASSERT((int)plank_bundle_hdr_version(&hdr) > 0);
    ASSERT_EQ((int)plank_bundle_hdr_type(&hdr), (int)PLANK_BUNDLE_LINK_SYNC);
    ASSERT_EQ(plank_bundle_hdr_dir_count(&hdr), 5u);
    ASSERT_EQ(plank_bundle_hdr_dir_entry_size(&hdr), PLANK_DIRENT_SIZE);
}

TEST(bundle_header_init_helper) {
    plank_bundle_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    plank_bundle_hdr_init(&hdr, PLANK_BUNDLE_USER_EXPORT, 0x0001, 256, 10);

    ASSERT(plank_bundle_hdr_valid_magic(&hdr));
    ASSERT_EQ((int)plank_bundle_hdr_type(&hdr), (int)PLANK_BUNDLE_USER_EXPORT);
    ASSERT_EQ(plank_bundle_hdr_flags(&hdr), 0x0001u);
    ASSERT_EQ(plank_bundle_hdr_manifest_len(&hdr), 256u);
    ASSERT_EQ(plank_bundle_hdr_dir_count(&hdr), 10u);
}

TEST(byte_order_conversions) {
    uint16_t val16 = 0x1234;
    uint16_t net16 = plank_htons(val16);
    uint16_t host16 = plank_ntohs(net16);
    ASSERT_EQ(host16, val16);

    uint32_t val32 = 0x12345678;
    uint32_t net32 = plank_htonl(val32);
    uint32_t host32 = plank_ntohl(net32);
    ASSERT_EQ(host32, val32);

    uint64_t val64 = 0x123456789ABCDEF0ULL;
    uint64_t net64 = plank_htonll(val64);
    uint64_t host64 = plank_ntohll(net64);
    ASSERT_EQ(host64, val64);
}

TEST(frame_types) {
    ASSERT_EQ(PLANK_FRAME_HELLO,          0x0001);
    ASSERT_EQ(PLANK_FRAME_HELLO_ACK,      0x0002);
    ASSERT_EQ(PLANK_FRAME_AUTH_PROOF,     0x0003);
    ASSERT_EQ(PLANK_FRAME_CAPS,           0x0004);
    ASSERT_EQ(PLANK_FRAME_BUNDLE_OFFER,   0x0005);
    ASSERT_EQ(PLANK_FRAME_BUNDLE_REQUEST, 0x0006);
    ASSERT_EQ(PLANK_FRAME_BUNDLE_DATA,    0x0007);
    ASSERT_EQ(PLANK_FRAME_RECEIPT,        0x0008);
    ASSERT_EQ(PLANK_FRAME_PING,           0x0009);
    ASSERT_EQ(PLANK_FRAME_PONG,           0x000A);
    ASSERT_EQ(PLANK_FRAME_ERROR,          0x000B);
    ASSERT_EQ(PLANK_FRAME_CLOSE,          0x000C);
}

TEST(bundle_types) {
    ASSERT_EQ((int)PLANK_BUNDLE_LINK_SYNC,      1);
    ASSERT_EQ((int)PLANK_BUNDLE_USER_EXPORT,    2);
    ASSERT_EQ((int)PLANK_BUNDLE_USER_REPLY,     3);
    ASSERT_EQ((int)PLANK_BUNDLE_ADMIN_TRANSFER, 4);
}

TEST(link_session_create) {
    /* Create an outbound session (no callbacks, link_id=1) */
    plank_link_session_t *session = plank_link_session_create_outbound(NULL, 1, NULL);
    ASSERT(session != NULL);
    plank_link_session_free(session);
}

TEST(link_session_state) {
    plank_link_session_t *session = plank_link_session_create_outbound(NULL, 1, NULL);
    ASSERT(session != NULL);
    /* Initial state is IDLE or DISABLED — not yet connected */
    plank_link_state_t state = plank_link_session_state(session);
    ASSERT(state == PLANK_LINK_IDLE || state == PLANK_LINK_DISABLED ||
           state == PLANK_LINK_CONNECTING);
    plank_link_session_free(session);
}

TEST(auth_transcript) {
    uint8_t initiator_id[PLANK_NODE_ID_SIZE], responder_id[PLANK_NODE_ID_SIZE];
    uint8_t link_id[PLANK_LINK_ID_SIZE];
    uint8_t init_nonce[32], resp_nonce[32];
    uint8_t transcript[256];
    size_t tlen = sizeof(transcript);

    plank_crypto_random(initiator_id, sizeof(initiator_id));
    plank_crypto_random(responder_id, sizeof(responder_id));
    plank_crypto_random(link_id, sizeof(link_id));
    plank_crypto_random(init_nonce, sizeof(init_nonce));
    plank_crypto_random(resp_nonce, sizeof(resp_nonce));

    bool ok = plank_crypto_build_auth_transcript(
        initiator_id, responder_id,
        link_id,
        init_nonce, resp_nonce,
        1000000000ULL, 1000000001ULL,
        transcript, &tlen);

    ASSERT(ok);
    ASSERT(tlen > 0);
    ASSERT(tlen <= sizeof(transcript));
}

TEST(frame_magic_validation) {
    plank_frame_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic[0] = 'P'; hdr.magic[1] = 'L';
    hdr.magic[2] = 'K'; hdr.magic[3] = '1';
    ASSERT(plank_frame_hdr_valid_magic(&hdr));

    hdr.magic[0] = 'X';
    ASSERT(!plank_frame_hdr_valid_magic(&hdr));
}

TEST(bundle_magic_validation) {
    plank_bundle_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    plank_bundle_hdr_init(&hdr, PLANK_BUNDLE_LINK_SYNC, 0, 0, 0);
    ASSERT(plank_bundle_hdr_valid_magic(&hdr));

    hdr.magic[2] = 'X';
    ASSERT(!plank_bundle_hdr_valid_magic(&hdr));
}

int main(void) {
    printf("PLANK Link/Wire Tests\n");
    printf("======================\n\n");

    if (!plank_init()) {
        fprintf(stderr, "Failed to initialize PLANK\n");
        return 1;
    }

    RUN_TEST(frame_header_size);
    RUN_TEST(frame_header_layout);
    RUN_TEST(bundle_header_size);
    RUN_TEST(bundle_header_layout);
    RUN_TEST(bundle_header_init_helper);
    RUN_TEST(byte_order_conversions);
    RUN_TEST(frame_types);
    RUN_TEST(bundle_types);
    RUN_TEST(link_session_create);
    RUN_TEST(link_session_state);
    RUN_TEST(auth_transcript);
    RUN_TEST(frame_magic_validation);
    RUN_TEST(bundle_magic_validation);

    plank_shutdown();

    printf("\nAll link/wire tests passed!\n");
    return 0;
}
