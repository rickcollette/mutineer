#pragma once
#include "bbs_session.h"

/* Maintenance commands */
void cmd_user_pack(Session* s, const char* data);
void cmd_message_pack(Session* s, const char* data);
void cmd_file_pack(Session* s, const char* data);
void cmd_rebuild_indexes(Session* s, const char* data);
void cmd_system_stats(Session* s, const char* data);
void cmd_maintenance_menu(Session* s, const char* data);
