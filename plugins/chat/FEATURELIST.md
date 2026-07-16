# Mutineer Teleconference System (MTS) Feature List

This roadmap was derived from the behavior and command text in
`DDD/DIALCHAT.MSG`, then reconciled with the current implementation in
`plugins/chat/chat.c` and the Mutineer plugin ABI.

`DIALCHAT.MSG` is a legacy message/template resource, not source code or a
formal protocol specification. Its text is useful evidence of user-visible
behavior, but implementation details must be designed for Mutineer. Historical
features involving harassment, arbitrary punishment, gender-based user lists,
or deliberately disconnecting users are not carried forward.

## Current implementation

The existing `com.mutineer.chat` plugin provides:

- One process-local public chat room.
- Up to 32 tracked participants.
- A thread-safe 100-message in-memory ring buffer.
- Timestamped public messages.
- Join and leave announcements.
- `/who`, `/help`, `/quit`, `/exit`, and `/q`.
- One-second input polling so new messages appear while a user is idle.
- Per-session plugin instances sharing global chat state.
- ANSI-colored output and host audit logging.

Current limitations:

- State disappears when the plugin or BBS restarts.
- There is only one room and no room metadata.
- No private messages, moderation, preferences, profiles, or history commands.
- User identity is a display string rather than a stable user-ID-based record.
- The hard limits are fixed in code.
- Tests cover plugin loading and entry, but not multi-session chat behavior.

## Product direction

The Mutineer Teleconference System (MTS) will be a fast, BBS-native social
space with modern safety controls. It should retain the immediacy and
personality suggested by DialChat without copying its obsolete account model.

### Command conventions

- Commands begin with `/` and are case-insensitive.
- Users can be addressed by exact handle, unique handle prefix, or node number.
- Ambiguous names must show matches and make no change.
- Destructive moderator commands require confirmation when practical.
- `/help <command>` documents syntax and permissions.
- Command aliases may preserve familiar DialChat forms, but one canonical form
  should appear in help.

## P0: dependable chat foundation

These features should come before broad DialChat parity.

### Rooms and presence

- `/rooms` — list room name, topic, visibility, and occupancy.
- `/join <room>` — move to a public room.
- `/who [room]` — list participants with handle, node, AFK state, and role.
- `/topic` — display the current room topic.
- Default `Lobby` room created at plugin startup.
- Stable internal room ID distinct from the editable display name.
- Presence tracked by numeric Mutineer user ID, with handle as display data.
- Idempotent cleanup on disconnect, plugin exit, or failed session teardown.
- Join/leave announcements scoped to the affected room.

### Messaging

- Public room messages.
- `/<user> <message>` and `/msg <user> <message>` — private message.
- `// <message>` or `/reply <message>` — reply to the last private contact.
- `>user message` — directed public message that remains visible to the room.
- `/me <action>` — standard emote/action.
- Distinct rendering for public, directed, private, action, and system events.
- Message length validation and control-character sanitization.
- Optional notification bell for private messages.
- Bounded recent history per room with explicit truncation behavior.

### Personal state and safety

- `/afk [message]` — toggle AFK state and optional response message.
- Automatically clear AFK when the user speaks.
- `/ignore <user>` — hide public messages and actions from a user.
- `/unignore <user>` and `/ignores` — manage the ignore list.
- `/block <user>` — reject private messages from a user.
- `/unblock <user>` and `/blocks` — manage the block list.
- Ignore/block state keyed by immutable user ID, not handle.
- A blocked sender receives a neutral delivery failure without learning private
  details about the recipient.

### Reliability and tests

- Unit tests for ring-buffer wraparound, ordering, and slow readers.
- Concurrent join/post/leave stress test.
- Two-session integration test for public delivery.
- Integration tests for private messages, ignore/block, AFK, and cleanup.
- No data race under ThreadSanitizer-compatible builds.
- Configurable limits for rooms, users, history, message length, and rate.

## P1: rooms and moderation

### User-created rooms

- `/create <room>` — create a temporary room and become moderator.
- `/rename <name>` — rename a moderated room.
- `/topic <text>` — set the room topic.
- `/public` and `/private` — control room visibility.
- `/invite <user>` — authorize and notify a user for a private room.
- `/uninvite <user>` — revoke an unused invitation.
- `/leave` — return to the Lobby.
- `/transfer <user>` — transfer room ownership/moderation.
- Temporary rooms disappear when empty.
- Configured permanent rooms survive restarts.
- Room names use normalized uniqueness and reject terminal control characters.

### Moderator controls

- `/kick <user> [reason]` — remove a user from the current room.
- `/ban <user> [duration] [reason]` — prevent room re-entry.
- `/unban <user>` and `/bans` — manage room bans.
- `/mute <user> [duration]` and `/unmute <user>` — control speaking without
  removing presence.
- Moderators cannot act on users outside their room.
- Room owners cannot target themselves with transfer-dependent operations.
- Every moderator action is recorded with actor, target, room, time, and reason.
- Users receive clear, non-humiliating moderation messages.

### Sysop controls

- `/announce <message>` — global chat announcement.
- `/chatlock [reason]` — temporarily stop new chat entries.
- `/rooms admin` — detailed room/occupancy/moderator view.
- `/close <room> [reason]` — close a room and move occupants to Lobby.
- `/globalmute`, `/globalban`, and reversals, using stable user IDs.
- Inspect recent moderation audit events.
- Sysop authorization comes from host-provided user flags; it must not rely on
  a special handle string.

## P1: persistent user experience

DialChat exposed many per-user display settings. Mutineer should implement the
useful subset without making ANSI mandatory.

- `/settings` — show current preferences.
- `/settings timestamps on|off`.
- `/settings joins on|off` — show or hide join/leave events.
- `/settings actions on|off`.
- `/settings bell on|off` — private-message alert.
- `/settings echo on|off` — local echo preference where terminal behavior
  makes it meaningful.
- `/settings color on|off` and a small accessible theme selection.
- `/settings default-room <room>`.
- `/settings greeting <text>` and `/settings farewell <text>`.
- `/settings reset` — restore defaults.
- Preferences persist by numeric user ID.
- ANSI-off output remains complete and readable; color never carries the only
  indication of message type or moderation state.

## P2: profiles, discovery, and history

Useful ideas corresponding to DialChat autobiographies, message slots, member
lists, and previous-user views:

- `/profile` — view your profile.
- `/profile set <text>` — set a short chat bio/status.
- `/profile <user>` — view another user's public chat profile.
- `/members [prefix]` — paginated chat-member directory.
- `/recent` — recent chat visitors with last-seen time, subject to privacy.
- `/history [count]` — recent messages in the current room.
- `/search <text>` — optional bounded room-history search.
- Per-user privacy switches for directory and last-seen visibility.
- Profiles and history must have explicit retention and deletion rules.

## P2: actions and social polish

DialChat supported editable general actions. A safer Mutineer form would be:

- Built-in `/me` action available from P0.
- `/actions` — list configured named actions.
- `<action> [user]` or `/action <name> [user]` — render a templated emote.
- Public and private action variants obey normal ignore/block rules.
- Sysops can add, edit, disable, and delete named action templates.
- Templates have strict placeholders and cannot inject ANSI/control sequences
  unless an explicit trusted-art mode is enabled.
- Optional `/stats` with messages sent, rooms visited, and session
  duration; avoid public popularity rankings by default.

## P3: optional extensions

These are lower priority because Mutineer already has dedicated subsystems or
because they require more host integration.

### One-to-one live chat mode

- `/chat <user>` sends a request.
- Recipient accepts with `/chat <user>` or declines with `/decline <user>`.
- Both users enter a focused conversation view and can return to their room.
- Respect a persistent `allow chat requests` preference.
- Do not block unrelated private messages unless focus mode explicitly says so.

### Cross-room watch

- `/watch <room>` and `/unwatch <room>` show activity from selected public
  rooms with clear room prefixes.
- `/watch all` must be sysop-only or rate-limited to avoid information leakage
  and terminal flooding.
- Private rooms are never watchable by uninvited users.

## Configuration and persistence

Suggested plugin configuration:

- `enabled`, `max_users`, `max_rooms`, `max_message_bytes`.
- `history_per_room`, `history_retention_days`.
- `messages_per_window`, `rate_window_seconds`.
- `allow_user_rooms`, `allow_private_rooms`, `max_rooms_per_user`.
- `default_room`, permanent-room definitions, default topics.
- Default user preferences and theme.
- Moderation audit retention.

Use the plugin-private data directory for a dedicated SQLite database once the
data model includes rooms, memberships, invitations, bans, preferences,
profiles, and history. The host KV API is suitable for a small number of simple
settings but offers no key enumeration or transactional multi-record updates.

## Required host/API work

Most P0 functionality can remain inside the plugin, but robust implementation
will benefit from ABI additions:

- Nonblocking or event-driven terminal input so message delivery does not rely
  on one-second `readline()` timeouts.
- A host-safe way to wake or write to another active plugin session.
- Session node number, security level, ANSI capability, and disconnect event.
- Structured audit logging with actor and target user IDs.
- Host configuration access for plugin-specific keys.
- Stable lifecycle notification when a session is forcibly disconnected.

Until those exist, shared state must remain mutex-protected and each active
instance must poll for queued messages.

## Explicitly excluded legacy behavior

The following ideas visible in `DIALCHAT.MSG` should not be reproduced:

- Gender-based "finder" lists.
- "Death words" that disconnect or punish users.
- Publicly demeaning kill, jail, nuke, idiot, or zap mechanics.
- Arbitrary reassignment of another user's account/line identity.
- Temporary privilege grants outside Mutineer's normal authorization model.
- A second independent account, validation, or time-limit system.
- Registration/demo-expiration and activation-code screens.

Moderation should be transparent, auditable, scoped, reversible where
possible, and expressed in neutral language.

## Recommended delivery sequence

1. Refactor global state around stable user IDs, rooms, and typed events.
2. Add public rooms, `/rooms`, `/join`, room-scoped `/who`, and tests.
3. Add private/directed messages, reply, AFK, ignore, and block.
4. Add persistence for preferences and configured permanent rooms.
5. Add room creation, invitations, transfer, kick/ban/mute, and audit records.
6. Add profiles, recent users, actions, and optional statistics.
7. Add host ABI extensions before truly event-driven delivery.

The first production milestone should stop after step 3: it would already turn
the current demonstration plugin into a credible multi-room BBS chat system
without taking on the risk of every legacy DialChat feature at once.
