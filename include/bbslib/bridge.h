#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "bbslib/lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BbsLibSession BbsLibSession;

typedef int (*BbsLibWriteFn)(void *user_data, const uint8_t *data, size_t len);
typedef int (*BbsLibReadlineFn)(void *user_data, uint8_t *buf, size_t cap, int timeout_sec, int echo);
typedef int (*BbsLibReadFn)(void *user_data, uint8_t *buf, size_t cap, int timeout_sec);
typedef void (*BbsLibFlushFn)(void *user_data);
typedef void (*BbsLibCloseFn)(void *user_data);

typedef struct BbsLibSessionAdapter
{
  BbsLibWriteFn write;
  BbsLibReadlineFn readline;
  BbsLibReadFn read;
  BbsLibFlushFn flush;
  BbsLibCloseFn close;
  void *user_data;
} BbsLibSessionAdapter;

typedef enum BbsLibSessionFlags
{
  BBSLIB_SESSION_NONE = 0,
  BBSLIB_SESSION_ALLOW_ADMIN_ACTIONS = 1 << 0
} BbsLibSessionFlags;

typedef struct BbsLibSessionOptions
{
  const char *handle;
  const char *ip;
  int node_num;
  unsigned flags;
} BbsLibSessionOptions;

BbsLibResult bbslib_session_open(BbsLibContext *ctx,
                                 const BbsLibSessionOptions *opts,
                                 const BbsLibSessionAdapter *adapter,
                                 BbsLibSession **out);
void bbslib_session_close(BbsLibSession *session);
BbsLibResult bbslib_session_run_action(BbsLibSession *session, const char *action);
BbsLibResult bbslib_session_launch_door(BbsLibSession *session, int door_id);

#ifdef __cplusplus
}
#endif
