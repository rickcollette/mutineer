#include "mts_command.h"
#include <stdio.h>

static const char *role(mts_role_t r) {
  return r == MTS_ROLE_SYSOP       ? "sysop"
         : r == MTS_ROLE_MODERATOR ? "moderator"
                                   : "user";
}
int main(void) {
  size_t n = 0;
  const mts_command_def_t *commands = mts_commands(&n);
  puts("<!-- Generated from plugins/chat/mts_command.c; do not edit manually. "
       "-->");
  puts("# MTS Command Reference\n");
  puts("| Command | Aliases | Role | Description |");
  puts("|---|---|---|---|");
  for (size_t i = 0; i < n; i++)
    printf("| `%s` | %s | %s | %s |\n", commands[i].syntax,
           commands[i].aliases && commands[i].aliases[0] ? commands[i].aliases
                                                         : "-",
           role(commands[i].role), commands[i].summary);
  puts("\n## Preferences\n");
  puts("`timestamps`, `joins`, `actions`, `bell`, `echo`, `color`, `theme`, "
       "`default-room`, `greeting`, `farewell`, `chat-requests`, `directory`, "
       "`last-seen`, and `profile`.\n");
  puts("Themes: `default`, `ocean`, `amber`, `high-contrast`, and `mono`.");
  return 0;
}
