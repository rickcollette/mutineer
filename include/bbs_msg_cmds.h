#pragma once
#include "bbs_session.h"

/* Message command handlers (M* and R* commands from ORIGINAL_BBS) */

void cmd_msg_area_change(Session* s, const char* data);     /* MA */
void cmd_msg_area_list(Session* s, const char* data);       /* MG */
void cmd_msg_new_scan(Session* s, const char* data);        /* MN */
void cmd_msg_post(Session* s, const char* data);            /* MP */
void cmd_msg_read(Session* s, const char* data);            /* MR */
void cmd_msg_search(Session* s, const char* data);          /* MS */
void cmd_msg_write_email(Session* s, const char* data);     /* MW */
void cmd_msg_reply(Session* s, const char* data);           /* RE */
void cmd_msg_read_new(Session* s, const char* data);        /* RN */
void cmd_msg_read_private(Session* s, const char* data);    /* RP */
void cmd_msg_your_messages(Session* s, const char* data);   /* RY */
void cmd_msg_toggle_scan_areas(Session* s, const char* data); /* MZ */

/* Main dispatcher for M* and R* commands */
void handle_msg_command(Session* s, const char* cmd, const char* data);

/* Full-screen editor (non-static, shared with msg_cmds.c) */
int fsedit_edit(Session* s, char* text_out, size_t text_cap);
