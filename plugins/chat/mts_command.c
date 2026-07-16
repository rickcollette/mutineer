#include "mts_command.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
static mts_command_def_t C[] = {
    {"help", "?", "/help [command]", "Show command help",
     "Help is generated from the MTS command registry.", MTS_ROLE_USER, 0, 1},
    {"rooms", "r", "/rooms [admin]", "List rooms",
     "Sysops may add admin for ownership and occupancy details.", MTS_ROLE_USER,
     0, 1},
    {"join", "j", "/join <room>", "Join a room",
     "Private rooms require ownership, invitation, or sysop access.",
     MTS_ROLE_USER, 1, 1},
    {"leave", "l", "/leave", "Return to Lobby",
     "Lobby is immutable and always available.", MTS_ROLE_USER, 0, 0},
    {"who", "s", "/who [room]", "List room occupants",
     "Shows node, AFK state, and role.", MTS_ROLE_USER, 0, 1},
    {"topic", "t", "/topic [text]", "View or set topic",
     "Setting requires room moderator authority.", MTS_ROLE_USER, 0, -1},
    {"msg", "m", "/msg <user> <message>", "Send private message",
     "Aliases include /<user> message.", MTS_ROLE_USER, 2, -1},
    {"reply", "//", "/reply <message>", "Reply privately",
     "Replies to the last private correspondent.", MTS_ROLE_USER, 1, -1},
    {"me", "em", "/me <action>", "Send an action",
     "Actions obey recipient visibility preferences.", MTS_ROLE_USER, 1, -1},
    {"afk", "", "/afk [message]", "Toggle AFK",
     "One automatic response is sent per sender and AFK epoch.", MTS_ROLE_USER,
     0, -1},
    {"ignore", "", "/ignore <user>", "Ignore public traffic",
     "Relationships are stored by stable user ID.", MTS_ROLE_USER, 1, 1},
    {"unignore", "", "/unignore <user>", "Remove ignore", "", MTS_ROLE_USER, 1,
     1},
    {"ignores", "", "/ignores", "List ignores", "", MTS_ROLE_USER, 0, 0},
    {"block", "", "/block <user>", "Block private traffic", "", MTS_ROLE_USER,
     1, 1},
    {"unblock", "", "/unblock <user>", "Remove block", "", MTS_ROLE_USER, 1, 1},
    {"blocks", "", "/blocks", "List blocks", "", MTS_ROLE_USER, 0, 0},
    {"settings", "", "/settings [key value|reset]", "Manage preferences",
     "Supports display, room, greeting, farewell, request, and privacy "
     "settings.",
     MTS_ROLE_USER, 0, -1},
    {"create", "", "/create <room>", "Create temporary room", "", MTS_ROLE_USER,
     1, 1},
    {"rename", "", "/rename <room>", "Rename current room", "",
     MTS_ROLE_MODERATOR, 1, 1},
    {"public", "", "/public", "Make room public", "", MTS_ROLE_MODERATOR, 0, 0},
    {"private", "", "/private", "Make room private", "", MTS_ROLE_MODERATOR, 0,
     0},
    {"invite", "", "/invite <user>", "Invite user", "", MTS_ROLE_MODERATOR, 1,
     1},
    {"uninvite", "", "/uninvite <user>", "Remove invitation", "",
     MTS_ROLE_MODERATOR, 1, 1},
    {"transfer", "", "/transfer <user> <reason>", "Transfer ownership", "",
     MTS_ROLE_MODERATOR, 2, -1},
    {"kick", "", "/kick <user> <reason>", "Kick user", "", MTS_ROLE_MODERATOR,
     2, -1},
    {"ban", "", "/ban <user> [duration] <reason>", "Ban user", "",
     MTS_ROLE_MODERATOR, 2, -1},
    {"unban", "", "/unban <user>", "Remove ban", "", MTS_ROLE_MODERATOR, 1, 1},
    {"bans", "", "/bans", "List bans", "", MTS_ROLE_MODERATOR, 0, 0},
    {"mute", "", "/mute <user> [duration] <reason>", "Mute user", "",
     MTS_ROLE_MODERATOR, 2, -1},
    {"unmute", "", "/unmute <user>", "Remove mute", "", MTS_ROLE_MODERATOR, 1,
     1},
    {"mutes", "", "/mutes", "List mutes", "", MTS_ROLE_MODERATOR, 0, 0},
    {"announce", "", "/announce <message>", "Global announcement", "",
     MTS_ROLE_SYSOP, 1, -1},
    {"chatlock", "", "/chatlock [reason]", "Toggle chat lock", "",
     MTS_ROLE_SYSOP, 0, -1},
    {"close", "", "/close <room> <reason>", "Close room", "", MTS_ROLE_SYSOP, 2,
     -1},
    {"globalmute", "", "/globalmute <user> [duration] <reason>",
     "Globally mute", "", MTS_ROLE_SYSOP, 2, -1},
    {"unglobalmute", "", "/unglobalmute <user>", "Remove global mute", "",
     MTS_ROLE_SYSOP, 1, 1},
    {"globalban", "", "/globalban <user> [duration] <reason>", "Globally ban",
     "", MTS_ROLE_SYSOP, 2, -1},
    {"unglobalban", "", "/unglobalban <user>", "Remove global ban", "",
     MTS_ROLE_SYSOP, 1, 1},
    {"mitsaudit", "", "/mitsaudit [count]", "View moderation audit", "",
     MTS_ROLE_SYSOP, 0, 1},
    {"profile", "", "/profile [set text|user]", "View or edit profile", "",
     MTS_ROLE_USER, 0, -1},
    {"members", "sc", "/members [prefix]", "List members", "", MTS_ROLE_USER, 0,
     1},
    {"recent", "", "/recent", "List recent users", "", MTS_ROLE_USER, 0, 0},
    {"history", "", "/history [count]", "Show room history", "", MTS_ROLE_USER,
     0, 1},
    {"search", "", "/search <text>", "Search room history", "", MTS_ROLE_USER,
     1, -1},
    {"actions", "", "/actions", "List named actions", "", MTS_ROLE_USER, 0, 0},
    {"action", "", "/action <name> [user]", "Run named action",
     "Sysops may add or disable validated templates.", MTS_ROLE_USER, 1, -1},
    {"stats", "", "/stats", "Show personal statistics", "", MTS_ROLE_USER, 0,
     0},
    {"chat", "", "/chat <user>", "Request focused chat", "", MTS_ROLE_USER, 1,
     1},
    {"accept", "", "/accept", "Accept focused chat", "", MTS_ROLE_USER, 0, 0},
    {"decline", "", "/decline", "Decline or end focused chat", "",
     MTS_ROLE_USER, 0, 0},
    {"watch", "", "/watch <room|all>", "Watch public room traffic", "",
     MTS_ROLE_USER, 1, 1},
    {"unwatch", "", "/unwatch", "Stop watching", "", MTS_ROLE_USER, 0, 0},
    {"quit", "q,exit", "/quit", "Leave MTS", "", MTS_ROLE_USER, 0, 0}};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
const mts_command_def_t *mts_commands(size_t *n)
{
  if (n)
    *n = sizeof C / sizeof *C;
  return C;
}
const mts_command_def_t *mts_command_find(const char *name)
{
  size_t n;
  const mts_command_def_t *c = mts_commands(&n);
  for (size_t i = 0; i < n; i++)
  {
    if (!strcasecmp(name, c[i].name))
      return &c[i];
    char a[64];
    snprintf(a, sizeof a, "%s", c[i].aliases ? c[i].aliases : "");
    char *save = NULL;
    for (char *p = strtok_r(a, ",", &save); p; p = strtok_r(NULL, ",", &save))
      if (!strcasecmp(name, p))
        return &c[i];
  }
  return NULL;
}
int mts_command_bind(const char *name, mts_command_handler_t handler)
{
  mts_command_def_t *command = (mts_command_def_t *)mts_command_find(name);
  if (!command)
    return 0;
  command->handler = handler;
  return 1;
}
