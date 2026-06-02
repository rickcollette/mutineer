/*
 * test_tools.c - Tests for standalone utility tools
 * 
 * These tests verify that the tool functions work correctly with a test database.
 * We test the core logic that each tool uses, not the command-line parsing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bbs_db.h"
#include "bbs_config.h"
#include "bbs_acs.h"
#include "bbs_flags.h"
#include "bbs_fido_netmail.h"
#include "bbs_msg_defs.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

static BbsDb* setup_test_db(void) {
    BbsDb* db = db_open(":memory:");
    if (!db) return NULL;
    
    if (!db_init_schema(db, "sql/schema.sql")) {
        db_close(db);
        return NULL;
    }
    
    if (!db_seed_defaults(db, "test_hash")) {
        db_close(db);
        return NULL;
    }
    
    return db;
}

/* Test message pack functionality */
static int test_msgpack_logic(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Create a message area */
    TEST_ASSERT(db_msg_area_seed(db, "Test Area"), "failed to seed msg area");
    
    DbMsgArea areas[10];
    int acount = db_msg_area_list(db, areas, 10);
    TEST_ASSERT(acount > 0, "no message areas");
    
    /* Create a test user */
    TEST_ASSERT(db_user_create(db, "testuser", "hash", 1), "failed to create user");
    DbUser user;
    TEST_ASSERT(db_user_fetch(db, "testuser", &user), "failed to fetch user");
    
    /* Post some messages */
    for (int i = 0; i < 5; i++) {
        char subj[64];
        snprintf(subj, sizeof(subj), "Test Message %d", i);
        TEST_ASSERT(db_message_post(db, areas[0].id, user.id, subj, "Test body", 0),
                    "failed to post message");
    }
    
    /* Verify messages exist */
    int before = db_count_messages(db);
    TEST_ASSERT(before >= 5, "expected at least 5 messages");
    
    /* Test that we can count and delete messages - the core msgpack logic */
    /* First verify the count works */
    int area_count = db_count_messages_area(db, areas[0].id);
    TEST_ASSERT(area_count >= 5, "expected at least 5 messages in area");
    
    /* Test deletion by area (simpler test) */
    int deleted = db_exec_simple(db, 
        "DELETE FROM messages WHERE area_id = 1 AND id <= 2");
    TEST_ASSERT(deleted >= 1, "expected to delete at least 1 message");
    
    int after = db_count_messages(db);
    TEST_ASSERT(after < before, "message count should decrease");
    TEST_ASSERT(after >= 3, "should have at least 3 messages left");
    
    db_close(db);
    return 0;
}

/* Test user pack functionality */
static int test_userpack_logic(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Create some test users */
    TEST_ASSERT(db_user_create(db, "user1", "hash", 1), "failed to create user1");
    TEST_ASSERT(db_user_create(db, "user2", "hash", 1), "failed to create user2");
    TEST_ASSERT(db_user_create(db, "user3", "hash", 1), "failed to create user3");
    
    int before = db_count_users(db);
    TEST_ASSERT(before >= 3, "expected at least 3 users");
    
    /* Mark user2 as deleted */
    DbUser user2;
    TEST_ASSERT(db_user_fetch(db, "user2", &user2), "failed to fetch user2");
    user2.status_flags |= STATUS_DELETED;
    TEST_ASSERT(db_user_update(db, &user2), "failed to update user2");
    
    /* Delete users with STATUS_DELETED flag */
    char sql[256];
    snprintf(sql, sizeof(sql), 
        "DELETE FROM users WHERE (status_flags & %d) != 0", STATUS_DELETED);
    int deleted = db_exec_simple(db, sql);
    TEST_ASSERT(deleted == 1, "expected to delete 1 user");
    
    int after = db_count_users(db);
    TEST_ASSERT(after == before - 1, "user count should decrease by 1");
    
    /* Verify user2 is gone */
    DbUser check;
    TEST_ASSERT(!db_user_fetch(db, "user2", &check), "user2 should be deleted");
    
    db_close(db);
    return 0;
}

/* Test file pack functionality */
static int test_filepack_logic(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Create a file area pointing to a temp directory */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/test_filepack_%d", (int)getpid());
    mkdir(tmpdir, 0755);
    
    TEST_ASSERT(db_file_area_seed(db, "Test Files", tmpdir), "failed to seed file area");
    
    DbFileArea areas[10];
    int acount = db_file_area_list(db, areas, 10);
    TEST_ASSERT(acount > 0, "no file areas");
    
    /* Add a file record for a file that exists */
    char realfile[512];
    snprintf(realfile, sizeof(realfile), "%s/realfile.txt", tmpdir);
    FILE* f = fopen(realfile, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
    TEST_ASSERT(db_file_add(db, areas[0].id, "realfile.txt", "Real file", 4, 1),
                "failed to add real file");
    
    /* Add a file record for a file that doesn't exist (orphan) */
    TEST_ASSERT(db_file_add(db, areas[0].id, "orphan.txt", "Orphan file", 100, 1),
                "failed to add orphan file");
    
    int before = db_count_files(db);
    TEST_ASSERT(before == 2, "expected 2 files");
    
    /* Simulate filepack: check each file and delete orphans */
    DbFileRec files[10];
    int fcount = db_file_list(&areas[0], db, files, 10);
    TEST_ASSERT(fcount == 2, "expected 2 file records");
    
    int orphans = 0;
    for (int i = 0; i < fcount; i++) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", areas[0].path, files[i].filename);
        if (access(filepath, F_OK) != 0) {
            db_file_delete(db, files[i].id);
            orphans++;
        }
    }
    TEST_ASSERT(orphans == 1, "expected 1 orphan");
    
    int after = db_count_files(db);
    TEST_ASSERT(after == 1, "expected 1 file after cleanup");
    
    /* Cleanup */
    unlink(realfile);
    rmdir(tmpdir);
    db_close(db);
    return 0;
}

/* Test stats functionality */
static int test_stats_logic(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Create some data */
    TEST_ASSERT(db_user_create(db, "statuser", "hash", 1), "failed to create user");
    TEST_ASSERT(db_msg_area_seed(db, "Stats Area"), "failed to seed msg area");
    
    /* Test counting functions */
    int users = db_count_users(db);
    TEST_ASSERT(users >= 1, "expected at least 1 user");
    
    int messages = db_count_messages(db);
    TEST_ASSERT(messages >= 0, "message count should be non-negative");
    
    int files = db_count_files(db);
    TEST_ASSERT(files >= 0, "file count should be non-negative");
    
    /* Test daily stats */
    DbDailyStats daily;
    TEST_ASSERT(db_daily_stats_get(db, &daily), "failed to get daily stats");
    
    /* Test system totals */
    DbSystemTotals totals;
    TEST_ASSERT(db_system_totals_get(db, &totals), "failed to get system totals");
    
    db_close(db);
    return 0;
}

/* Test database maintenance functionality */
static int test_maint_logic(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Test VACUUM */
    TEST_ASSERT(db_exec(db, "VACUUM"), "VACUUM failed");
    
    /* Test REINDEX */
    TEST_ASSERT(db_exec(db, "REINDEX"), "REINDEX failed");
    
    /* Test ANALYZE */
    TEST_ASSERT(db_exec(db, "ANALYZE"), "ANALYZE failed");
    
    /* Test integrity check - just verify it doesn't crash */
    TEST_ASSERT(db_exec(db, "PRAGMA integrity_check"), "integrity check failed");
    
    db_close(db);
    return 0;
}

/* Test ACS check function used by tools */
static int test_acs_check(void) {
    /* Test basic security level checks */
    TEST_ASSERT(acs_check("S10", 50, 0, 0), "S10 should pass for level 50");
    TEST_ASSERT(acs_check("S50", 50, 0, 0), "S50 should pass for level 50");
    TEST_ASSERT(!acs_check("S90", 50, 0, 0), "S90 should fail for level 50");
    
    /* Test AR flag checks */
    unsigned ar_a = (1u << 0);  /* Flag A */
    unsigned ar_b = (1u << 1);  /* Flag B */
    TEST_ASSERT(acs_check("FA", 10, ar_a, 0), "FA should pass with flag A");
    TEST_ASSERT(!acs_check("FB", 10, ar_a, 0), "FB should fail without flag B");
    TEST_ASSERT(acs_check("FB", 10, ar_b, 0), "FB should pass with flag B");
    
    /* Test legacy +X format */
    TEST_ASSERT(acs_check("+A", 10, ar_a, 0), "+A should pass with flag A");
    TEST_ASSERT(!acs_check("+B", 10, ar_a, 0), "+B should fail without flag B");
    
    /* Test empty ACS (should always pass) */
    TEST_ASSERT(acs_check("", 0, 0, 0), "empty ACS should pass");
    TEST_ASSERT(acs_check(NULL, 0, 0, 0), "NULL ACS should pass");
    
    return 0;
}

/* Test QWK generation helpers */
static int test_qwk_helpers(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");
    
    /* Create user and message area */
    TEST_ASSERT(db_user_create(db, "qwkuser", "hash", 1), "failed to create user");
    TEST_ASSERT(db_msg_area_seed(db, "QWK Area"), "failed to seed msg area");
    
    DbUser user;
    TEST_ASSERT(db_user_fetch(db, "qwkuser", &user), "failed to fetch user");
    
    DbMsgArea areas[10];
    int acount = db_msg_area_list(db, areas, 10);
    TEST_ASSERT(acount > 0, "no message areas");
    
    /* Post a message */
    TEST_ASSERT(db_message_post(db, areas[0].id, user.id, "QWK Test", "Body text", 0),
                "failed to post message");
    
    /* Verify we can list messages for QWK generation */
    DbMessage msgs[100];
    int mcount = db_messages_list(db, areas[0].id, msgs, 100);
    TEST_ASSERT(mcount >= 1, "expected at least 1 message");
    TEST_ASSERT(strcmp(msgs[0].subject, "QWK Test") == 0, "subject mismatch");
    
    db_close(db);
    return 0;
}

static int test_fido_netmail_export(void) {
    BbsDb* db = setup_test_db();
    TEST_ASSERT(db != NULL, "failed to setup test db");

    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/test_netmail_export_%d", (int)getpid());
    mkdir(tmpdir, 0755);

    DbFidoNetmail nm;
    memset(&nm, 0, sizeof(nm));
    nm.from_zone = 1;
    nm.from_net = 234;
    nm.from_node = 56;
    snprintf(nm.from_name, sizeof(nm.from_name), "Sysop");
    nm.to_zone = 1;
    nm.to_net = 999;
    nm.to_node = 10;
    snprintf(nm.to_name, sizeof(nm.to_name), "Remote");
    snprintf(nm.subject, sizeof(nm.subject), "Export Test");
    snprintf(nm.body, sizeof(nm.body), "Hello netmail");
    nm.attr = NET_ATTR_LOCAL;
    TEST_ASSERT(db_fido_netmail_add(db, &nm), "failed to add netmail");

    FidoNetmailExportResult result;
    TEST_ASSERT(fido_netmail_export_pending(db, tmpdir, 10, false, &result), "export failed");
    TEST_ASSERT(result.scanned == 1, "expected one scanned netmail");
    TEST_ASSERT(result.exported == 1, "expected one exported netmail");
    TEST_ASSERT(result.failed == 0, "expected no export failures");
    TEST_ASSERT(result.last_path[0] != '\0', "expected exported path");
    TEST_ASSERT(access(result.last_path, R_OK) == 0, "exported packet missing");

    DbFidoNetmail listed[4];
    TEST_ASSERT(db_fido_netmail_list(db, "pending", listed, 4) == 0, "pending netmail should be empty");
    TEST_ASSERT(db_fido_netmail_list(db, "sent", listed, 4) == 1, "sent netmail should contain export");

    unlink(result.last_path);
    rmdir(tmpdir);
    db_close(db);
    return 0;
}

int main(void) {
    int failures = 0;
    int total = 0;
    
    printf("Running tool tests...\n\n");
    
#define RUN_TEST(name) do { \
    total++; \
    printf("  %s... ", #name); \
    fflush(stdout); \
    if (name() == 0) { \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
        failures++; \
    } \
} while(0)
    
    RUN_TEST(test_msgpack_logic);
    RUN_TEST(test_userpack_logic);
    RUN_TEST(test_filepack_logic);
    RUN_TEST(test_stats_logic);
    RUN_TEST(test_maint_logic);
    RUN_TEST(test_acs_check);
    RUN_TEST(test_qwk_helpers);
    RUN_TEST(test_fido_netmail_export);
    
    printf("\n%d/%d tests passed\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}
