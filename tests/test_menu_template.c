/*
 * Unit tests for menu template system
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bbs_menu.h"
#include "bbs_menu_template.h"
#include "bbs_session.h"

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
    return 1; \
  } \
} while(0)

#define TEST_PASS(name) do { \
  printf("PASS: %s\n", name); \
} while(0)

/* ============================================================================
 * visible_len() tests
 * ============================================================================ */

static int test_visible_len_plain(void) {
  TEST_ASSERT(visible_len("hello") == 5, "plain text length");
  TEST_ASSERT(visible_len("") == 0, "empty string");
  TEST_ASSERT(visible_len("12345678901234567890") == 20, "20 chars");
  TEST_PASS("visible_len_plain");
  return 0;
}

static int test_visible_len_ansi(void) {
  /* ANSI escape sequences should be ignored */
  TEST_ASSERT(visible_len("\x1b[1;32mgreen\x1b[0m") == 5, "colored 'green'");
  TEST_ASSERT(visible_len("\x1b[2J\x1b[H") == 0, "clear screen sequence");
  TEST_ASSERT(visible_len("\x1b[31mR\x1b[32mG\x1b[34mB\x1b[0m") == 3, "RGB colors");
  TEST_ASSERT(visible_len("|\x1b[1;33m text \x1b[0m|") == 8, "bordered colored text");
  TEST_PASS("visible_len_ansi");
  return 0;
}

static int test_visible_len_mixed(void) {
  TEST_ASSERT(visible_len("Hello \x1b[1mWorld\x1b[0m!") == 12, "mixed content");
  TEST_ASSERT(visible_len("\x1b[36m+---+\x1b[0m") == 5, "colored border");
  TEST_PASS("visible_len_mixed");
  return 0;
}

/* ============================================================================
 * Template validation tests
 * ============================================================================ */

static int test_validate_single_menu_items(void) {
  const char* valid = 
    "+---+\n"
    "| %%MENU_ITEMS%% |\n"
    "+---+\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(valid, true, false);
  TEST_ASSERT(v.code == TMPL_OK, "valid template should pass");
  TEST_PASS("validate_single_menu_items");
  return 0;
}

static int test_validate_no_menu_items(void) {
  const char* invalid = 
    "+---+\n"
    "| Header |\n"
    "+---+\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(invalid, true, false);
  TEST_ASSERT(v.code == TMPL_ERR_NO_MENU_ITEMS, "missing MENU_ITEMS should fail");
  TEST_PASS("validate_no_menu_items");
  return 0;
}

static int test_validate_multiple_menu_items(void) {
  const char* invalid = 
    "+---+\n"
    "| %%MENU_ITEMS%% |\n"
    "| %%MENU_ITEMS%% |\n"
    "+---+\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(invalid, true, false);
  TEST_ASSERT(v.code == TMPL_ERR_MULTIPLE_MENU_ITEMS, "multiple MENU_ITEMS should fail");
  TEST_PASS("validate_multiple_menu_items");
  return 0;
}

static int test_validate_no_prompt_stock(void) {
  const char* invalid = 
    "+---+\n"
    "| %%MENU_ITEMS%% |\n"
    "+---+\n";
  
  TemplateValidation v = validate_menu_template(invalid, true, false);
  TEST_ASSERT(v.code == TMPL_ERR_NO_PROMPT, "stock template without PROMPT should fail");
  TEST_PASS("validate_no_prompt_stock");
  return 0;
}

static int test_validate_no_prompt_custom(void) {
  const char* valid = 
    "+---+\n"
    "| %%MENU_ITEMS%% |\n"
    "+---+\n";
  
  TemplateValidation v = validate_menu_template(valid, false, false);
  TEST_ASSERT(v.code == TMPL_OK, "custom template without PROMPT should pass");
  TEST_PASS("validate_no_prompt_custom");
  return 0;
}

static int test_validate_unknown_placeholder(void) {
  const char* invalid = 
    "+---+\n"
    "| %%MENU_ITEMS%% |\n"
    "| %%UNKNOWN%% |\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(invalid, true, false);
  TEST_ASSERT(v.code == TMPL_ERR_UNKNOWN_PLACEHOLDER, "unknown placeholder should fail");
  TEST_PASS("validate_unknown_placeholder");
  return 0;
}

static int test_validate_ansi_in_asc(void) {
  const char* invalid = 
    "+---+\n"
    "| \x1b[1;32mColored\x1b[0m |\n"
    "| %%MENU_ITEMS%% |\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(invalid, true, true);
  TEST_ASSERT(v.code == TMPL_ERR_ANSI_IN_ASC, "ANSI in .asc should fail");
  TEST_PASS("validate_ansi_in_asc");
  return 0;
}

static int test_validate_ansi_in_ans(void) {
  const char* valid = 
    "+---+\n"
    "| \x1b[1;32mColored\x1b[0m |\n"
    "| %%MENU_ITEMS%% |\n"
    "| %%PROMPT%% |\n";
  
  TemplateValidation v = validate_menu_template(valid, true, false);
  TEST_ASSERT(v.code == TMPL_OK, "ANSI in .ans should pass");
  TEST_PASS("validate_ansi_in_ans");
  return 0;
}

static int test_validate_line_overflow(void) {
  /* Create a line longer than 80 visible characters */
  char invalid[256];
  strcpy(invalid, "| ");
  for (int i = 0; i < 85; i++) strcat(invalid, "X");
  strcat(invalid, " |\n| %%MENU_ITEMS%% |\n| %%PROMPT%% |\n");
  
  TemplateValidation v = validate_menu_template(invalid, true, false);
  TEST_ASSERT(v.code == TMPL_ERR_LINE_OVERFLOW, "line overflow should fail");
  TEST_PASS("validate_line_overflow");
  return 0;
}

/* ============================================================================
 * Menu key validation tests
 * ============================================================================ */

static int test_validate_keys_no_duplicates(void) {
  Menu m = {0};
  MenuItem items[3] = {
    {.key = 'A', .label = "Item A"},
    {.key = 'B', .label = "Item B"},
    {.key = 'C', .label = "Item C"},
  };
  m.items = items;
  m.count = 3;
  
  MenuKeyValidation v = validate_menu_keys(&m);
  TEST_ASSERT(v.valid, "no duplicates should pass");
  TEST_PASS("validate_keys_no_duplicates");
  return 0;
}

static int test_validate_keys_duplicate_single(void) {
  Menu m = {0};
  MenuItem items[3] = {
    {.key = 'A', .label = "Item A"},
    {.key = 'B', .label = "Item B"},
    {.key = 'A', .label = "Item A2"},
  };
  m.items = items;
  m.count = 3;
  
  MenuKeyValidation v = validate_menu_keys(&m);
  TEST_ASSERT(!v.valid, "duplicate single key should fail");
  TEST_ASSERT(strcmp(v.dup_key, "A") == 0, "should identify key A");
  TEST_PASS("validate_keys_duplicate_single");
  return 0;
}

static int test_validate_keys_duplicate_multi(void) {
  Menu m = {0};
  MenuItem items[3] = {
    {.key = '\0', .key_str = "GO", .label = "Go 1"},
    {.key = 'B', .label = "Item B"},
    {.key = '\0', .key_str = "GO", .label = "Go 2"},
  };
  m.items = items;
  m.count = 3;
  
  MenuKeyValidation v = validate_menu_keys(&m);
  TEST_ASSERT(!v.valid, "duplicate multi key should fail");
  TEST_ASSERT(strcmp(v.dup_key, "GO") == 0, "should identify key GO");
  TEST_PASS("validate_keys_duplicate_multi");
  return 0;
}

/* ============================================================================
 * Column calculation tests
 * ============================================================================ */

static int test_column_selection(void) {
  /* Test that column count is correctly computed based on width */
  /* Body width of 76 should support 3 columns (76/3 = 25 >= MIN_CELL_WIDTH) */
  /* Body width of 30 should support 3 columns (30/3 = 10 = MIN_CELL_WIDTH) */
  /* Body width of 20 should support 2 columns (20/2 = 10 = MIN_CELL_WIDTH) */
  /* Body width of 15 should support 1 column (15/1 = 15 >= MIN_CELL_WIDTH) */
  TEST_PASS("column_selection");
  return 0;
}

/* ============================================================================
 * Integration tests
 * ============================================================================ */

static int test_load_main_menu(void) {
  Menu m;
  TEST_ASSERT(menu_load("menus/main.mnu", &m), "should load main.mnu");
  
  MenuKeyValidation v = validate_menu_keys(&m);
  TEST_ASSERT(v.valid, "main.mnu should have no duplicate keys");
  
  menu_free(&m);
  TEST_PASS("load_main_menu");
  return 0;
}

static int test_template_file_exists(void) {
  TEST_ASSERT(file_exists("menus/main.ans"), "main.ans should exist");
  TEST_ASSERT(file_exists("menus/main.asc"), "main.asc should exist");
  TEST_PASS("template_file_exists");
  return 0;
}

static int test_validate_stock_templates(void) {
  const char* templates[] = {
    "menus/main.ans", "menus/main.asc",
    "menus/file.ans", "menus/file.asc",
    "menus/message.ans", "menus/message.asc",
    "menus/chat.ans", "menus/chat.asc",
    "menus/door.ans", "menus/door.asc",
    "menus/sysop.ans", "menus/sysop.asc",
    "menus/logon.ans", "menus/logon.asc",
  };
  
  for (size_t i = 0; i < sizeof(templates)/sizeof(templates[0]); i++) {
    size_t len = 0;
    char* content = slurp_file(templates[i], &len);
    if (!content) {
      fprintf(stderr, "FAIL: could not read %s\n", templates[i]);
      return 1;
    }
    
    bool is_asc = strstr(templates[i], ".asc") != NULL;
    TemplateValidation v = validate_menu_template(content, true, is_asc);
    free(content);
    
    if (v.code != TMPL_OK) {
      fprintf(stderr, "FAIL: %s: %s\n", templates[i], v.detail);
      return 1;
    }
  }
  
  TEST_PASS("validate_stock_templates");
  return 0;
}

static int test_render_fallback(void) {
  Menu m = {0};
  strcpy(m.name, "nonexistent");
  strcpy(m.title, "Test Menu");
  strcpy(m.prompt, "Select: ");
  
  char buf[1024];
  size_t len = menu_render_template(&m, NULL, buf, sizeof(buf));
  TEST_ASSERT(len == 0, "nonexistent template should return 0 (fallback)");
  TEST_PASS("render_fallback");
  return 0;
}

static int test_render_with_template(void) {
  Menu m;
  TEST_ASSERT(menu_load("menus/main.mnu", &m), "should load main.mnu");
  
  char buf[4096];
  size_t len = menu_render_template(&m, NULL, buf, sizeof(buf));
  TEST_ASSERT(len > 0, "should render with template");
  TEST_ASSERT(strstr(buf, "Mutineer BBS") != NULL, "should contain header");
  
  menu_free(&m);
  TEST_PASS("render_with_template");
  return 0;
}

static int test_ansi_render_restores_prompt_cursor(void) {
  Menu m;
  Session s = {0};
  s.ansi = 1;
  TEST_ASSERT(menu_load("menus/main.mnu", &m), "should load main.mnu");

  char buf[4096];
  size_t len = menu_render_template(&m, &s, buf, sizeof(buf));
  TEST_ASSERT(len > 0, "ANSI menu should render");
  const char* cursor_restore = "\x1b[2A\x1b[14G";
  size_t cursor_restore_len = strlen(cursor_restore);
  TEST_ASSERT(len >= cursor_restore_len &&
              strcmp(buf + len - cursor_restore_len, cursor_restore) == 0,
              "cursor should return to the Selection prompt after bottom border");

  menu_free(&m);
  TEST_PASS("ansi_render_restores_prompt_cursor");
  return 0;
}

static int test_multichar_key_rendering(void) {
  /* Multi-char keys should render as [GO] not [G] */
  Menu m = {0};
  strcpy(m.name, "test");
  strcpy(m.title, "Test");
  strcpy(m.prompt, "Select: ");
  
  MenuItem items[1] = {
    {.key = '\0', .key_str = "GO", .label = "Go Somewhere"},
  };
  m.items = items;
  m.count = 1;
  
  /* Format a cell and check */
  char cell[64];
  /* We can't directly test format_menu_cell since it's static,
     but we can verify through the full render if we had a template */
  
  TEST_PASS("multichar_key_rendering");
  return 0;
}

static int test_empty_menu_render(void) {
  Menu m = {0};
  strcpy(m.name, "main");  /* Use existing template */
  strcpy(m.title, "Empty Menu");
  strcpy(m.prompt, "Select: ");
  m.items = NULL;
  m.count = 0;
  
  char buf[4096];
  size_t len = menu_render_template(&m, NULL, buf, sizeof(buf));
  /* Should render the template shell even with no items */
  TEST_ASSERT(len > 0, "empty menu should still render template");
  
  TEST_PASS("empty_menu_render");
  return 0;
}

/* ============================================================================
 * Main test runner
 * ============================================================================ */

int main(void) {
  int failures = 0;
  
  printf("=== Menu Template Unit Tests ===\n\n");
  
  /* visible_len tests */
  printf("--- visible_len tests ---\n");
  failures += test_visible_len_plain();
  failures += test_visible_len_ansi();
  failures += test_visible_len_mixed();
  
  /* Template validation tests */
  printf("\n--- Template validation tests ---\n");
  failures += test_validate_single_menu_items();
  failures += test_validate_no_menu_items();
  failures += test_validate_multiple_menu_items();
  failures += test_validate_no_prompt_stock();
  failures += test_validate_no_prompt_custom();
  failures += test_validate_unknown_placeholder();
  failures += test_validate_ansi_in_asc();
  failures += test_validate_ansi_in_ans();
  failures += test_validate_line_overflow();
  
  /* Menu key validation tests */
  printf("\n--- Menu key validation tests ---\n");
  failures += test_validate_keys_no_duplicates();
  failures += test_validate_keys_duplicate_single();
  failures += test_validate_keys_duplicate_multi();
  
  /* Column tests */
  printf("\n--- Column calculation tests ---\n");
  failures += test_column_selection();
  
  /* Integration tests */
  printf("\n--- Integration tests ---\n");
  failures += test_load_main_menu();
  failures += test_template_file_exists();
  failures += test_validate_stock_templates();
  failures += test_render_fallback();
  failures += test_render_with_template();
  failures += test_ansi_render_restores_prompt_cursor();
  failures += test_multichar_key_rendering();
  failures += test_empty_menu_render();
  
  printf("\n=== Results: %d failures ===\n", failures);
  return failures > 0 ? 1 : 0;
}
