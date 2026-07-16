#pragma once
#include <stddef.h>
typedef enum { MTS_ROLE_USER, MTS_ROLE_MODERATOR, MTS_ROLE_SYSOP } mts_role_t;
struct mts_session;
typedef void (*mts_command_handler_t)(struct mts_session *session, char *args,
                                      int *running, const char *canonical);
typedef struct {
  const char *name, *aliases, *syntax, *summary, *detail;
  mts_role_t role;
  int min_args, max_args;
  mts_command_handler_t handler;
} mts_command_def_t;
const mts_command_def_t *mts_commands(size_t *count);
const mts_command_def_t *mts_command_find(const char *name);
int mts_command_bind(const char *name, mts_command_handler_t handler);
