#include <stdio.h>
#include <stdlib.h>
#include "bbs_menu.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

int main(void) {
  Menu m;
  TEST_ASSERT(menu_load("menus/main.mnu", &m), "menu_load failed");
  
  const MenuItem* it = menu_find(&m, 'W');
  TEST_ASSERT(it != NULL, "menu_find('W') returned NULL");
  TEST_ASSERT(it->action[0] != '\0', "action is empty");
  
  char buf[512];
  size_t n = menu_render(&m, NULL, buf, sizeof(buf));
  TEST_ASSERT(n > 0, "menu_render returned 0");
  
  menu_free(&m);
  return 0;
}
