#pragma once

#include "bbslib/lifecycle.h"
#include "bbs_db.h"

#ifdef __cplusplus
extern "C" {
#endif

int bbslib_msg_area_list(BbsLibContext *ctx, DbMsgArea *out, int max_areas);
int bbslib_messages_list(BbsLibContext *ctx, int area_id, DbMessage *out, int max_msgs);
BbsLibResult bbslib_message_post(BbsLibContext *ctx, int area_id, int user_id,
                                 const char *subject, const char *body,
                                 int reply_to);

#ifdef __cplusplus
}
#endif
