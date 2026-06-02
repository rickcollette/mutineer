#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbs_db.h"
#include "bbs_menu.h"
#include "bbs_mci.h"
#include "bbs_acs.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

static int test_db_operations(void) {
  BbsDb* db = db_open(":memory:");
  TEST_ASSERT(db != NULL, "db_open failed");
  
  TEST_ASSERT(db_init_schema(db, "sql/schema.sql"), "schema init failed");
  
  /* Seed defaults (creates security levels, etc.) */
  TEST_ASSERT(db_seed_defaults(db, "sysop_hash"), "seed defaults failed");
  
  /* Test user operations */
  TEST_ASSERT(db_user_create(db, "testuser", "hash123", 1), "user create failed");
  
  DbUser user;
  TEST_ASSERT(db_user_fetch(db, "testuser", &user), "user fetch failed");
  TEST_ASSERT(strcmp(user.handle, "testuser") == 0, "user handle mismatch");
  
  /* Test message area */
  TEST_ASSERT(db_msg_area_seed(db, "General"), "msg area seed failed");
  
  DbMsgArea areas[10];
  int acount = db_msg_area_list(db, areas, 10);
  TEST_ASSERT(acount > 0, "no message areas");
  
  /* Test message posting */
  TEST_ASSERT(db_message_post(db, areas[0].id, user.id, "Test Subject", "Test body", 0), 
              "message post failed");
  
  DbMessage msgs[10];
  int mcount = db_messages_list(db, areas[0].id, msgs, 10);
  TEST_ASSERT(mcount > 0, "no messages");
  TEST_ASSERT(strcmp(msgs[0].subject, "Test Subject") == 0, "subject mismatch");
  
  db_close(db);
  return 0;
}

static int test_menu_operations(void) {
  Menu m;
  TEST_ASSERT(menu_load("menus/main.mnu", &m), "menu load failed");
  TEST_ASSERT(m.count > 0, "menu has no items");
  
  const MenuItem* item = menu_find(&m, 'W');
  TEST_ASSERT(item != NULL, "menu_find failed for 'W'");
  TEST_ASSERT(strcmp(item->action, "who") == 0, "action mismatch");
  
  char buf[1024];
  size_t n = menu_render(&m, NULL, buf, sizeof(buf));
  TEST_ASSERT(n > 0, "menu_render returned 0");
  
  menu_free(&m);
  return 0;
}

static int test_mci_expansion(void) {
  Session s;
  memset(&s, 0, sizeof(s));
  strcpy(s.user.handle, "TestUser");
  s.user.level = 50;
  s.node_num = 1;
  s.time_left_min = 60;
  
  char out[256];
  mci_expand(&s, "Hello %UN, you have %TL minutes left", out, sizeof(out));
  TEST_ASSERT(strstr(out, "TestUser") != NULL, "handle not expanded");
  TEST_ASSERT(strstr(out, "60") != NULL, "time not expanded");
  
  return 0;
}

static int test_acs_evaluation(void) {
  Session s;
  memset(&s, 0, sizeof(s));
  s.user.level = 50;
  s.user.flags = (1u << 0);  /* +A flag */
  s.time_left_min = 30;
  
  TEST_ASSERT(acs_allows(&s, "L10"), "L10 should pass");
  TEST_ASSERT(acs_allows(&s, "L50"), "L50 should pass");
  TEST_ASSERT(!acs_allows(&s, "L90"), "L90 should fail");
  TEST_ASSERT(acs_allows(&s, "+A"), "+A should pass");
  TEST_ASSERT(!acs_allows(&s, "+B"), "+B should fail");
  
  return 0;
}

int main(void) {
  int failures = 0;
  
  printf("Running smoke tests...\n");
  
  printf("  test_db_operations... ");
  if (test_db_operations() == 0) {
    printf("PASS\n");
  } else {
    printf("FAIL\n");
    failures++;
  }
  
  printf("  test_menu_operations... ");
  if (test_menu_operations() == 0) {
    printf("PASS\n");
  } else {
    printf("FAIL\n");
    failures++;
  }
  
  printf("  test_mci_expansion... ");
  if (test_mci_expansion() == 0) {
    printf("PASS\n");
  } else {
    printf("FAIL\n");
    failures++;
  }
  
  printf("  test_acs_evaluation... ");
  if (test_acs_evaluation() == 0) {
    printf("PASS\n");
  } else {
    printf("FAIL\n");
    failures++;
  }
  
  printf("\n%d/%d tests passed\n", 4 - failures, 4);
  return failures > 0 ? 1 : 0;
}
