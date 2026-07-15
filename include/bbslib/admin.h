#pragma once

#include <stdbool.h>
#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

int bbslib_node_list(BbsLibContext *ctx, DbNode *out, int max_nodes);
BbsLibResult bbslib_node_lock_set(BbsLibContext *ctx, int node_num, bool locked,
                                  const char *actor);
BbsLibResult bbslib_maintenance_vacuum(BbsLibContext *ctx);
BbsLibResult bbslib_maintenance_reindex(BbsLibContext *ctx);
BbsLibResult bbslib_maintenance_analyze(BbsLibContext *ctx);
BbsLibResult bbslib_maintenance_integrity(BbsLibContext *ctx);
BbsLibResult bbslib_maintenance_backup(BbsLibContext *ctx, const char *output_path);

#ifdef __cplusplus
}
#endif
