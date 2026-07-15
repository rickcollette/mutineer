#pragma once

#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

int bbslib_file_area_list(BbsLibContext *ctx, DbFileArea *out, int max_areas);
int bbslib_file_list(BbsLibContext *ctx, int area_id, DbFileRec *out, int max_files);
BbsLibResult bbslib_file_get(BbsLibContext *ctx, int file_id, DbFileRec *out);

#ifdef __cplusplus
}
#endif
