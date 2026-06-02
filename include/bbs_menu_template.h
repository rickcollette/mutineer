#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "bbs_menu.h"

struct Session;

/* Template validation error codes */
typedef enum {
  TMPL_OK = 0,
  TMPL_ERR_FILE_NOT_FOUND,
  TMPL_ERR_FILE_READ,
  TMPL_ERR_NO_MENU_ITEMS,
  TMPL_ERR_MULTIPLE_MENU_ITEMS,
  TMPL_ERR_NO_PROMPT,
  TMPL_ERR_UNKNOWN_PLACEHOLDER,
  TMPL_ERR_LINE_OVERFLOW,
  TMPL_ERR_BODY_WIDTH_INVALID,
  TMPL_ERR_ANSI_IN_ASC,
  TMPL_ERR_MALFORMED
} TemplateError;

/* Validation result with details */
typedef struct {
  TemplateError code;
  int line_num;
  char detail[128];
} TemplateValidation;

/* Menu key validation result */
typedef struct {
  bool valid;
  char dup_key[16];
  char label1[64];
  char label2[64];
} MenuKeyValidation;

/* Terminal width constant */
#define TERMINAL_WIDTH 80

/* Minimum cell width for safe rendering */
#define MIN_CELL_WIDTH 10

/* Maximum columns for menu layout */
#define MAX_MENU_COLS 3

/* Core helper functions */

/* Calculate visible length of string, ignoring ANSI escape sequences */
size_t visible_len(const char* str);

/* Check if file exists */
bool file_exists(const char* path);

/* Load entire file into allocated buffer. Caller must free. Returns NULL on error. */
char* slurp_file(const char* path, size_t* out_len);

/* Resolve template path based on menu and session ANSI capability.
   Returns true if template file exists, false otherwise. */
bool resolve_menu_template_path(const Menu* m, const struct Session* s,
                                char* out, size_t cap);

/* Collect visible menu items after ACS/flag filtering.
   Returns count of items stored in out array. */
size_t collect_visible_menu_items(const Menu* m, const struct Session* s,
                                  const MenuItem** out, size_t cap);

/* Template validation */

/* Validate a template file. is_stock indicates if %%PROMPT%% is required.
   is_asc indicates if ANSI escapes should be rejected. */
TemplateValidation validate_menu_template(const char* content, bool is_stock, bool is_asc);

/* Validate menu keys for duplicates */
MenuKeyValidation validate_menu_keys(const Menu* m);

/* Template rendering */

/* Render menu using template. Returns bytes written, or 0 on failure.
   On failure, caller should fall back to menu_render(). */
size_t menu_render_template(const Menu* m, const struct Session* s,
                            char* buf, size_t cap);

/* Get human-readable error message for template error */
const char* template_error_str(TemplateError err);

/* Logging callback type for template errors */
typedef void (*TemplateLogFn)(const char* menu_name, const char* msg);

/* Set logging callback for template errors */
void template_set_log_fn(TemplateLogFn fn);
