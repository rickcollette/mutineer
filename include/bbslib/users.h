#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

BbsLibResult bbslib_user_get(BbsLibContext *ctx, const char *handle, DbUser *out);
BbsLibResult bbslib_user_update(BbsLibContext *ctx, const DbUser *user);
BbsLibResult bbslib_authenticate_user(BbsLibContext *ctx, const char *handle,
                                      const char *password, DbUser *out,
                                      bool *password_needs_upgrade);

#ifdef __cplusplus
}
#endif
