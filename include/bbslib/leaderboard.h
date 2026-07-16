#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BbsLibLeaderboardConfig
{
  int door_id;
  bool enabled;
  char game_key[64];
  char game_name[64];
  char score_label[32];
  char score_order[8];
} BbsLibLeaderboardConfig;

BbsLibResult bbslib_leaderboard_config(BbsLibContext *ctx, int door_id,
                                       BbsLibLeaderboardConfig *out);
BbsLibResult bbslib_leaderboard_submit(BbsLibContext *ctx, int door_id,
                                       const char *handle, int64_t score,
                                       const char *detail);
int bbslib_leaderboard_list(BbsLibContext *ctx, DbDoorScore *out, int max);

#ifdef __cplusplus
}
#endif
