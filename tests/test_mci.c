#include <stdio.h>
#include <string.h>
#include "bbs_mci.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

int main(void) {
  Session s;
  memset(&s, 0, sizeof(s));
  strcpy(s.user.handle, "tester");
  s.user.level = 25;
  s.node_num = 3;
  s.time_left_min = 42;
  s.user.credits = 1234;
  char out[256];
  mci_expand(&s, "User:%UN Node:%NN TL:%TL CR:%CR", out, sizeof(out));
  TEST_ASSERT(strstr(out, "tester") != NULL, "should contain 'tester'");
  TEST_ASSERT(strstr(out, "3") != NULL, "should contain '3'");
  TEST_ASSERT(strstr(out, "42") != NULL, "should contain '42'");
  TEST_ASSERT(strstr(out, "1234") != NULL, "should contain '1234'");
  mci_expand(&s, "%?L30{hi|lo}", out, sizeof(out));
  TEST_ASSERT(strcmp(out, "lo") == 0, "L30 conditional should be 'lo'");
  mci_expand(&s, "%?L20{hi|lo}", out, sizeof(out));
  TEST_ASSERT(strcmp(out, "hi") == 0, "L20 conditional should be 'hi'");
  return 0;
}
