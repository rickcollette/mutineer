<!-- generated-by: gsd-doc-writer -->

# Chat and Social

Mutineer BBS provides real-time communication between online users: split-screen chat, teleconference rooms, sysop paging, wall broadcasts, whispers, oneliners, and short messages.

## Chat Modes

### Line Chat (`linechat` action)

Simple multi-line chat mode. Users type messages broadcast to all participants in the chat session. Exited with a quit command.

### Split-Screen Chat (`splitchat` action)

Two-panel ANSI chat between two nodes implemented in `src/chat.c`:

```c
void split_chat_start(Session* s, Session* other);
```

- Caller selects target node by number
- Screen splits: local input on bottom, remote messages on top
- Real-time bidirectional communication
- Either party can exit to return to BBS

Menu action `chat` enters the chat subsystem (may offer mode selection).

### Teleconference (`cmd_teleconf`)

Multi-user chat rooms with DB-backed state:

| Function | Purpose |
|----------|---------|
| `teleconf_create_room()` | Create room with name, topic, privacy, password |
| `teleconf_join_room()` | Join by room ID |
| `teleconf_leave_room()` | Leave current room |
| `teleconf_broadcast()` | Send message to all room members |
| `teleconf_list_rooms()` | List active rooms |
| `teleconf_who_in_room()` | Show participants |

Initialized at startup via `teleconf_init()` with DB handle from `teleconf_set_db()`.

## Sysop Paging

Menu action `page` sends a page request to the sysop:

1. User enters page message
2. System attempts to notify sysop on WFC/console
3. If sysop unavailable, message may fall back to email
4. Limited by config `max_page_sysop` (default 3 per session; 0=unlimited)

Users with `STATUS_NOPAGE` or AC restrictions may be blocked from paging.

## Wall and Whisper

### Wall (`wall` action)

Broadcast message to all online users:

```
[Wall] Handle: message text
```

Uses `online_broadcast()` to send to all active sessions.

### Whisper (`whisper` action)

Private message to a specific node:

1. Prompt for target node number
2. Prompt for message text
3. Delivered only to session on that node via `online_get_node()`

## Oneliners

One-line public messages stored in `oneliners` table:

| Field | Purpose |
|-------|---------|
| `user_id` / `user_handle` | Author |
| `text` | Oneliner content |
| `posted_at` | Timestamp |

Menu action `oneliners` displays recent oneliners and allows posting new ones.

## Short Messages (SMW)

Short Messages Waiting — private brief messages between users:

| Table | Purpose |
|-------|---------|
| `short_messages` | Message store with read flag |

Menu action `smw` checks and displays waiting short messages. User field `smw` tracks waiting count.

Fields: `from_user`, `to_user`, `from_handle`, `to_handle`, `message`, `read_flag`.

## Chat Logging

When `chat_log_path` is set in config, chat sessions log to files in that directory.

Additionally, `chat_logs` table stores structured logs:

| Field | Purpose |
|-------|---------|
| `chat_type` | `split`, `teleconf`, or `sysop` |
| `room_id` | Teleconference room (if applicable) |
| `from_user` / `from_handle` | Sender |
| `to_user` / `to_handle` | Recipient (split chat) |
| `message` | Content |
| `logged_at` | Timestamp |

Empty `chat_log_path` disables file logging; DB logging may still occur depending on code path.

## Restrictions

| Flag | Effect |
|------|--------|
| `AC_RCHAT` | Restricted from chat (ACS `RC`) |
| `STATUS_NOCHAT` | Not available for chat requests |
| `STATUS_NOPAGE` | Cannot be paged |
| `AC_RUSERLIST` | Cannot view who's online (`who` action) |

## Who's Online

Menu action `who` (key `W` on main menu) lists active nodes:

```
Online users (nodes):
  Node 1  CaptainHook    IP 192.168.1.10   online
  Node 2  FirstMate      IP 192.168.1.11   chat
```

Data from `nodes` table via `db_node_list()`.

## WFC Chat Indicator

WFC console shows chat status with configurable character (`wfc_status_chat_char`, default `S`). Node status transitions: idle → logging → online → chat.

## Interactive Tests

Expect tests cover chat flows:

```bash
ctest --test-dir build -R expect_suite
# includes test_chat.exp, interactive_chat.exp
```

## Plugin Chat

Sample plugin `plugins/chat/chat.c` demonstrates plugin-based chat as alternative to built-in chat. Invoked via:

```
2|Chat Plugin|plugin:com.mutineer.chat|L10
```

See [Plugins](plugins.md) for plugin vs native chat comparison.

## Related Documentation

- [Configuration](configuration.md) — `chat_log_path`, `max_page_sysop`
- [Sysop Guide](sysop-guide.md) — WFC monitoring
- [Menus and UI](menus-and-ui.md) — menu keys for chat actions
- [Reference: Menu Actions](reference/menu-actions.md)
