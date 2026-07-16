<!-- Generated from plugins/chat/mts_command.c; do not edit manually. -->
# MTS Command Reference

| Command | Aliases | Role | Description |
|---|---|---|---|
| `/help [command]` | ? | user | Show command help |
| `/rooms [admin]` | - | user | List rooms |
| `/join <room>` | - | user | Join a room |
| `/leave` | - | user | Return to Lobby |
| `/who [room]` | - | user | List room occupants |
| `/topic [text]` | - | user | View or set topic |
| `/msg <user> <message>` | m | user | Send private message |
| `/reply <message>` | // | user | Reply privately |
| `/me <action>` | - | user | Send an action |
| `/afk [message]` | - | user | Toggle AFK |
| `/ignore <user>` | - | user | Ignore public traffic |
| `/unignore <user>` | - | user | Remove ignore |
| `/ignores` | - | user | List ignores |
| `/block <user>` | - | user | Block private traffic |
| `/unblock <user>` | - | user | Remove block |
| `/blocks` | - | user | List blocks |
| `/settings [key value|reset]` | - | user | Manage preferences |
| `/create <room>` | - | user | Create temporary room |
| `/rename <room>` | - | moderator | Rename current room |
| `/public` | - | moderator | Make room public |
| `/private` | - | moderator | Make room private |
| `/invite <user>` | - | moderator | Invite user |
| `/uninvite <user>` | - | moderator | Remove invitation |
| `/transfer <user> <reason>` | - | moderator | Transfer ownership |
| `/kick <user> <reason>` | - | moderator | Kick user |
| `/ban <user> [duration] <reason>` | - | moderator | Ban user |
| `/unban <user>` | - | moderator | Remove ban |
| `/bans` | - | moderator | List bans |
| `/mute <user> [duration] <reason>` | - | moderator | Mute user |
| `/unmute <user>` | - | moderator | Remove mute |
| `/mutes` | - | moderator | List mutes |
| `/announce <message>` | - | sysop | Global announcement |
| `/chatlock [reason]` | - | sysop | Toggle chat lock |
| `/close <room> <reason>` | - | sysop | Close room |
| `/globalmute <user> [duration] <reason>` | - | sysop | Globally mute |
| `/unglobalmute <user>` | - | sysop | Remove global mute |
| `/globalban <user> [duration] <reason>` | - | sysop | Globally ban |
| `/unglobalban <user>` | - | sysop | Remove global ban |
| `/mitsaudit [count]` | - | sysop | View moderation audit |
| `/profile [set text|user]` | - | user | View or edit profile |
| `/members [prefix]` | - | user | List members |
| `/recent` | - | user | List recent users |
| `/history [count]` | - | user | Show room history |
| `/search <text>` | - | user | Search room history |
| `/actions` | - | user | List named actions |
| `/action <name> [user]` | - | user | Run named action |
| `/stats` | - | user | Show personal statistics |
| `/chat <user>` | - | user | Request focused chat |
| `/accept` | - | user | Accept focused chat |
| `/decline` | - | user | Decline or end focused chat |
| `/watch <room|all>` | - | user | Watch public room traffic |
| `/unwatch` | - | user | Stop watching |
| `/quit` | q,exit | user | Leave MTS |

## Preferences

`timestamps`, `joins`, `actions`, `bell`, `echo`, `color`, `theme`, `default-room`, `greeting`, `farewell`, `chat-requests`, `directory`, `last-seen`, and `profile`.

Themes: `default`, `ocean`, `amber`, `high-contrast`, and `mono`.
