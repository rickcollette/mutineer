#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Menu flags (matching ORIGINAL_BBS MenuFlags) */
#define MENU_FLAG_CLRSCR_BEFORE   (1u << 0)  /* Clear screen before displaying */
#define MENU_FLAG_DONT_CENTER     (1u << 1)  /* Don't center menu title */
#define MENU_FLAG_NO_MENU_TITLE   (1u << 2)  /* Don't show menu title */
#define MENU_FLAG_FORCE_PAUSE     (1u << 3)  /* Force pause after display */
#define MENU_FLAG_AUTO_TIME       (1u << 4)  /* Auto-display time remaining */
#define MENU_FLAG_NO_MENU_PROMPT  (1u << 5)  /* Don't show "Selection:" prompt */
#define MENU_FLAG_USE_LIGHTBAR    (1u << 6)  /* Use lightbar navigation */
#define MENU_FLAG_HOTKEYS         (1u << 7)  /* Enable hotkeys (no ENTER needed) */

/* Command flags (matching ORIGINAL_BBS CmdFlags) */
#define CMD_FLAG_HIDDEN           (1u << 0)  /* Don't display in menu */
#define CMD_FLAG_UNHIDDEN         (1u << 1)  /* Always display (override ACS) */
#define CMD_FLAG_PASSWORD         (1u << 2)  /* Requires password */
#define CMD_FLAG_SYSOP_LOG        (1u << 3)  /* Log to sysop log */

typedef struct MenuItem {
  char key;              /* single char or '\0' for multi-char */
  char key_str[8];       /* multi-char key (e.g., "QUIT") */
  char label[128];
  char action[64];
  char data[256];        /* action parameter/data */
  char acs[64];
  unsigned flags;        /* CMD_FLAG_* */
  char password[32];     /* required password if CMD_FLAG_PASSWORD */
} MenuItem;

typedef struct Menu {
  char name[32];         /* menu filename/identifier */
  char title[128];
  char prompt[64];       /* custom prompt (default "Selection: ") */
  char art_file[64];     /* ANSI art file to display */
  char fallback[32];     /* fallback menu name */
  unsigned flags;        /* MENU_FLAG_* */
  int gen_cols;          /* columns for generic menu display */
  MenuItem* items;
  size_t count;
} Menu;

struct Session;

/* Load menu from file. Enhanced format supports:
   # Comment
   @TITLE Menu Title
   @FLAGS flags (comma-separated: clrscr,nocenter,notitle,pause,autotime,noprompt,lightbar,hotkeys)
   @PROMPT Custom prompt:
   @ART filename.ans
   @FALLBACK menuname
   @COLS 2
   KEY|Label|Action|Data|ACS|CmdFlags|Password
   
   Legacy format still supported:
   KEY|Label|Action|ACS
*/
bool menu_load(const char* path, Menu* out);
bool menu_save(const char* path, const Menu* menu);
bool menu_delete_file(const char* path);
void menu_free(Menu* m);

/* Returns pointer to item for key, or NULL. */
const MenuItem* menu_find(const Menu* m, char key);

/* Find by multi-char key string */
const MenuItem* menu_find_str(const Menu* m, const char* key_str);

/* Render menu text into a provided buffer. */
size_t menu_render(const Menu* m, const struct Session* s, char* buf, size_t cap);

/* Check if menu item should be displayed (based on ACS and flags) */
bool menu_item_visible(const MenuItem* item, const struct Session* s);
