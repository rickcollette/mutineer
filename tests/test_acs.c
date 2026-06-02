#include <stdio.h>
#include <string.h>
#include "bbs_session.h"
#include "bbs_acs.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

static Session make_session(void) {
  Session s;
  memset(&s, 0, sizeof(s));
  s.user.level = 50;
  s.user.flags = (1u << ('A' - 'A')); /* +A */
  s.time_left_min = 30;
  s.user.credits = 1000;
  s.user.dl_ratio_num = 2;
  s.user.dl_ratio_den = 1;
  return s;
}

int main(void) {
  Session s = make_session();
  TEST_ASSERT(acs_allows(&s, "L10"), "L10 should pass");
  TEST_ASSERT(acs_allows(&s, "SL50"), "SL50 should pass");
  TEST_ASSERT(!acs_allows(&s, "L90"), "L90 should fail");
  TEST_ASSERT(acs_allows(&s, "+A"), "+A should pass");
  TEST_ASSERT(!acs_allows(&s, "+B"), "+B should fail");
  TEST_ASSERT(acs_allows(&s, "(!+B)|+A"), "(!+B)|+A should pass");
  TEST_ASSERT(acs_allows(&s, "C>500"), "C>500 should pass");
  TEST_ASSERT(!acs_allows(&s, "T>40"), "T>40 should fail");
  TEST_ASSERT(acs_allows(&s, "(SL40&+A)|L10"), "(SL40&+A)|L10 should pass");

  /* C# - total logins >= # */
  s.user.logged_on = 5;
  TEST_ASSERT(acs_allows(&s, "C5"),  "C5 with logged_on=5 should pass");
  TEST_ASSERT(acs_allows(&s, "C3"),  "C3 with logged_on=5 should pass");
  TEST_ASSERT(!acs_allows(&s, "C6"), "C6 with logged_on=5 should fail");
  TEST_ASSERT(acs_allows(&s, "C0"),  "C0 with logged_on=5 should pass");
  /* C? conference letter still works alongside C# */
  s.current_conf = 0;
  TEST_ASSERT(acs_allows(&s, "CA"), "CA conference check should pass for conf 0");
  TEST_ASSERT(!acs_allows(&s, "CB"), "CB conference check should fail for conf 0");

  /* E# - subscription (db=NULL, should return false for any E# check) */
  TEST_ASSERT(!acs_allows(&s, "E1"), "E1 with no db should fail");
  TEST_ASSERT(!acs_allows(&s, "E0"), "E0 with no db should fail");

  return 0;
}
