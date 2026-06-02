#pragma once
#include "bbs_session.h"
#include "bbs_db.h"

/* Split-screen chat between two nodes */
void split_chat_start(Session* s, Session* other);

/* Teleconference system */
void teleconf_init(void);
void teleconf_set_db(BbsDb* db);
int teleconf_create_room(Session* s, const char* name, const char* topic, int private_room, const char* password);
int teleconf_join_room(Session* s, int room_id, const char* password);
void teleconf_leave_room(Session* s, int room_id);
void teleconf_broadcast(int room_id, const char* from, const char* msg);
void teleconf_list_rooms(Session* s);
void teleconf_who_in_room(Session* s, int room_id);

/* Teleconference command handler */
void cmd_teleconf(Session* s, const char* data);
