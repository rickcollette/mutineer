#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BbsLibMetrics
{
  char bbs_name[128];
  int users;
  int messages;
  int files;
  int message_areas;
  int file_areas;
  int oneliners;
  DbDailyStats today;
  DbSystemTotals totals;
} BbsLibMetrics;

BbsLibResult bbslib_metrics_get(BbsLibContext *ctx, BbsLibMetrics *out);
BbsLibResult bbslib_status_json(BbsLibContext *ctx, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
