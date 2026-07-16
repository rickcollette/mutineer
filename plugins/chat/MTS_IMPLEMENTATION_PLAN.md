# Mutineer Teleconference System (MTS) Implementation Plan

## Objective

Replace the demonstration-quality single-room `plugins/chat/chat.c` with the
Mutineer Teleconference System (MTS), implementing the approved capabilities in
`plugins/chat/FEATURELIST.md` without importing DialChat's account model or
abusive legacy controls.

The first production milestone is multi-room public chat, private and directed
messages, actions, AFK, ignore, block, and dependable concurrent cleanup. Later
milestones add persistent preferences, moderated rooms, profiles, history,
named actions, and optional focused one-to-one chat.

## Naming and compatibility decisions

- User-facing name: `Mutineer Teleconference System`.
- Short name: `MTS`.
- Keep plugin ID `com.mutineer.chat` for compatibility with existing menus,
  allowlists, data directories, and installations.
- Initially keep the shared-object target and filename `chat_plugin.so`.
- Change descriptor name to `Mutineer Teleconference System` and description to
  identify it as MTS.
- Start the new implementation at plugin version `2.0.0` because the behavior,
  internal state, and commands change substantially.
- Change the main menu label from `Chat Plugin` to `Teleconference (MTS)` only
  when Milestone 2 is usable; do not advertise the new system while it is still
  only an internal refactor.
- Preserve `/quit`, `/exit`, `/q`, `/who`, `/help`, and ordinary public-message
  input throughout the migration.

## Scope boundaries

MTS owns:

- Teleconference rooms and presence.
- Public, directed, private, action, and system events.
- AFK, ignore, block, chat preferences, profiles, and chat history.
- Room moderation and MTS-specific sysop controls.
- MTS persistence and moderation audit records.

MTS does not own:

- BBS account creation, password management, validation, or call limits.
- General node management outside an active MTS session.
- Arbitrary user privilege changes.
- Product-wide chat, wall, or mail behavior outside this plugin.

## Architectural approach

Do not grow the current 390-line file into a monolith. Keep the plugin ABI
entrypoint small and split the implementation into testable modules.

Proposed source layout:

```text
plugins/chat/
  chat.c                 ABI descriptor and lifecycle entrypoints
  mts.h                  shared internal types and limits
  mts_state.c/.h         global state, rooms, presence, event sequencing
  mts_command.c/.h       tokenizer, dispatcher, command metadata, help
  mts_delivery.c/.h      recipient selection, filtering, per-user inboxes
  mts_render.c/.h        ANSI/plain rendering and terminal sanitization
  mts_identity.c/.h      exact/prefix/node user resolution
  mts_policy.c/.h        permissions, ignore/block, moderation checks
  mts_store.c/.h         SQLite schema, migrations, persistence queries
  mts_config.c/.h        defaults and plugin configuration loading
  mts_time.c/.h          duration parsing and expiration helpers
  FEATURELIST.md
  MTS_IMPLEMENTATION_PLAN.md
```

Tests:

```text
tests/mts/
  test_mts_state.c
  test_mts_commands.c
  test_mts_delivery.c
  test_mts_policy.c
  test_mts_store.c
  test_mts_concurrency.c
  test_mts_plugin.c
tests/test_mts.exp           two-or-more live BBS sessions
```

The exact split may be consolidated if a module remains trivial, but state,
command parsing, delivery policy, persistence, and rendering must remain
separable enough for unit tests.

## Core data model

### Process state

`mts_state_t` replaces `g_chat` and owns:

- One mutex protecting room/presence topology and configuration changes.
- A monotonically increasing 64-bit event sequence.
- Active rooms indexed by stable room ID and normalized name.
- Active participants indexed by numeric user ID and session instance.
- Global chat lock state and reason.
- Configuration snapshot.
- Persistence handle.
- Shutdown flag.

Avoid holding the global mutex while writing to a terminal, reading input, or
running SQLite statements. Copy immutable delivery work under lock, release the
lock, then render or persist.

### Session instance

`mts_session_t` replaces `chat_inst_t` and contains:

- Host session pointer valid only during instance lifetime.
- Numeric user ID, current display handle, user flags, and eventual node number.
- Current room ID and role.
- AFK state/message.
- Last private correspondent user ID.
- Per-session event inbox with head/tail/count and its own mutex.
- Last delivered event sequence.
- Rate-limit counters.
- Active/exiting flags supporting idempotent cleanup.
- Loaded preferences.

Treat two concurrent sessions for the same user ID explicitly. Initial policy:
allow both as distinct presences but address private messages to all active MTS
sessions for that user. Document and test this behavior.

### Room

`mts_room_t` contains:

- Stable 64-bit room ID.
- Normalized lookup name and display name.
- Topic.
- Public/private visibility.
- Permanent/temporary type.
- Owner/moderator user ID.
- Occupancy count.
- Creation and update timestamps.
- Bounded recent-event ring.
- Invitation, room-ban, and room-mute collections.

Room zero is invalid. The Lobby has a stable reserved ID and cannot be deleted,
made private, transferred, or renamed.

### Typed event

Replace sender/text-only messages with `mts_event_t`:

- Sequence and timestamp.
- Type: public, directed, private, action, system, presence, moderation, or
  announcement.
- Room ID when scoped to a room.
- Sender and target numeric user IDs.
- Snapshot display handles for rendering/history.
- Sanitized UTF-8 text.
- Flags such as private, bell, or suppressed-from-history.

All delivery decisions operate on typed fields, never on parsing formatted
output strings.

## Persistence model

Use SQLite in the directory returned by
`plugin_data_dir("com.mutineer.chat", ...)`, stored as `mts.db`. Link the MTS
plugin target against SQLite through the existing CMake SQLite discovery.

Initial schema:

```text
mts_schema_version(version, applied_at)
mts_rooms(id, normalized_name, display_name, topic, visibility,
          permanent, owner_user_id, created_at, updated_at)
mts_preferences(user_id, timestamps, joins, actions, bell, echo, color,
                theme, default_room_id, greeting, farewell,
                allow_chat_requests, directory_visible, last_seen_visible,
                updated_at)
mts_ignores(owner_user_id, target_user_id, created_at)
mts_blocks(owner_user_id, target_user_id, created_at)
mts_profiles(user_id, bio, updated_at)
mts_invitations(room_id, user_id, invited_by, created_at)
mts_room_bans(room_id, user_id, actor_user_id, reason, expires_at, created_at)
mts_room_mutes(room_id, user_id, actor_user_id, reason, expires_at, created_at)
mts_global_bans(user_id, actor_user_id, reason, expires_at, created_at)
mts_global_mutes(user_id, actor_user_id, reason, expires_at, created_at)
mts_history(id, room_id, sequence, event_type, sender_user_id,
            target_user_id, sender_handle, target_handle, text, created_at)
mts_visits(id, user_id, handle, entered_at, left_at)
mts_actions(id, name, template, enabled, created_by, updated_at)
mts_moderation_audit(id, actor_user_id, target_user_id, room_id,
                     action, reason, expires_at, created_at)
```

Migration rules:

- Schema initialization and migration run during plugin `init()` before the
  plugin accepts instances.
- Each migration is transactional and recorded by integer version.
- Migration failure disables MTS cleanly and logs the exact version/error.
- P0 can create the database and schema early while leaving history persistence
  disabled until the applicable milestone.
- Never key persistent relationships by handle.
- Enforce maximum lengths before binding data.
- Retention cleanup runs in bounded batches.

## Configuration

Because plugin ABI v1.0 does not expose arbitrary host configuration, use
`mts.conf` inside the plugin data directory for the first implementation.

Proposed keys:

```ini
enabled=true
max_users=128
max_rooms=64
max_message_bytes=512
history_per_room=200
history_retention_days=30
messages_per_window=8
rate_window_seconds=10
allow_user_rooms=true
allow_private_rooms=true
max_rooms_per_user=1
default_room=Lobby
moderation_audit_retention_days=365
persist_history=false
```

Requirements:

- Missing file uses documented safe defaults.
- Unknown keys warn but do not fail startup.
- Invalid values fail startup or fall back only when explicitly documented.
- Configuration is immutable during an active plugin run until a deliberate
  reload feature exists.
- Limits must be capped to protect memory even if configured excessively.

## Command architecture

Create a table-driven dispatcher with:

- Canonical name and aliases.
- Syntax and short/long help.
- Minimum argument count.
- Required role: user, room moderator, or sysop.
- Allowed contexts, such as Lobby, any room, or private room.
- Handler function.

Parsing requirements:

- Case-insensitive commands and user/room lookup.
- Preserve original case in message bodies, topics, bios, and reasons.
- Split command name from remainder once; handlers own detailed argument
  parsing.
- Support quoted room names only if room names are allowed to contain spaces.
  Recommended P0 rule: display names may contain spaces, `/join` consumes the
  entire remaining argument.
- Reject embedded CR, LF, ESC, C0 controls, and invalid UTF-8 at ingress.
- Distinguish `// message`, `/<user> message`, `/command`, `>user message`, and
  ordinary public text before dispatch.
- `/help` is generated from the command table so syntax cannot drift.

## Authorization model

- Stable identity: `session_user_id()`.
- Display identity: `session_username()`.
- Sysop authority: AR flag `A` from `session_user_flags()` using the shared flag
  definition; never infer authority from handle `sysop`.
- Room authority: owner/moderator ID stored on the room.
- Global moderation always requires sysop authority.
- A sysop may override room moderation where explicitly documented, but the
  action must still be audited.
- Ignore and block apply to sysops' ordinary messages. Required operational
  announcements use a distinct system/announcement type and cannot be spoofed
  by normal users.

## Delivery rules

For every event:

1. Validate and sanitize input.
2. Resolve sender, target, and room under the state lock.
3. Check global and room mute/ban policy.
4. Create one immutable typed event with a unique sequence.
5. Determine recipients.
6. Apply recipient-specific ignore/block/preferences filters.
7. Enqueue copies into bounded per-session inboxes.
8. Add eligible events to room history.
9. Persist eligible events outside the global lock.
10. Render from each recipient's own session loop.

Overflow policy:

- Inbox capacity is configurable with a hard maximum.
- Never overwrite an undelivered private message silently.
- When public traffic overflows, collapse dropped public events into one visible
  system notice reporting the number skipped.
- Slow sessions must not block senders or hold the global mutex.

## Rendering

- Centralize output templates in `mts_render.c`.
- Provide ANSI and plain modes for every event type.
- Color is decoration, never the only distinction.
- Sanitize handles, room names, topics, reasons, bios, and message bodies before
  terminal output.
- Use consistent MTS branding, for example:

```text
Mutineer Teleconference System (MTS)
Room: Lobby  Topic: Welcome aboard  Online: 4
```

- Prompt includes room name and AFK marker without exceeding narrow terminals.
- Add pagination for `/rooms`, `/who`, `/members`, `/history`, and audit views.
- The initial ABI cannot expose ANSI capability. Until host support lands,
  provide `/settings color off` and choose a conservative default.

## Milestone plan

### Milestone 0 — lock the baseline

Purpose: make the current behavior testable before refactoring.

Work:

- Fix existing trailing whitespace and enable warnings-as-errors for MTS test
  targets.
- Add a direct test target that loads `chat_plugin.so`, validates ID/name/version,
  calls `init()`, creates instances, and shuts down cleanly.
- Build a fake host API supporting I/O capture, stable user IDs, flags, KV, and
  plugin data directories.
- Add tests for the current message ring: ordering, wraparound, more than ten
  unread events, and no duplicate join/leave cleanup.
- Record current command compatibility in tests.

Exit gate:

- Existing `/who`, `/help`, public message, and exit behavior is covered.
- `ctest -R 'plugin|mts'` passes repeatedly.
- A two-thread post/read stress test has no deadlock.

### Milestone 1 — MTS core refactor

Purpose: introduce typed state without changing the visible single-room product.

Work:

- Rename user-facing descriptor and screen text to MTS 2.0.0.
- Introduce `mts_state_t`, `mts_session_t`, `mts_room_t`, and `mts_event_t`.
- Create stable Lobby room.
- Track presence by user ID and session, not handle.
- Replace the global ring with per-session inboxes plus Lobby history.
- Implement idempotent enter/leave/destroy cleanup.
- Add shared sanitization, rendering, and user resolution.
- Keep compatible current commands working.

Exit gate:

- No behavior regression for existing users.
- Two simultaneous sessions exchange ordered public events.
- A slow reader cannot block a sender.
- Duplicate sessions and abrupt I/O failure clean up correctly.
- Thread stress and sanitizer builds pass.

### Milestone 2 — public multi-room MTS

Purpose: deliver the first recognizably new MTS interface.

Commands:

- `/rooms`
- `/join <room>`
- `/who [room]`
- `/topic`
- `/leave`
- `/help [command]`

Work:

- Add configured permanent public rooms and room-scoped histories.
- Scope presence events and public messages to the current room.
- Implement exact, normalized room lookup with ambiguity errors.
- Add table-driven command dispatch and generated help.
- Rename menu label to `Teleconference (MTS)`.
- Update plugin documentation and interactive tests.

Exit gate:

- Users in different rooms cannot see each other's room traffic.
- `/rooms` occupancy is correct under concurrent joins/leaves.
- Lobby invariants are enforced.
- Menu and plugin integration tests pass.

### Milestone 3 — direct communication and safety

Purpose: reach the first production milestone from `FEATURELIST.md`.

Commands:

- `/msg <user> <message>` and `/<user> <message>`
- `/reply <message>` and `// <message>`
- `>user message`
- `/me <action>`
- `/afk [message]`
- `/ignore`, `/unignore`, `/ignores`
- `/block`, `/unblock`, `/blocks`

Work:

- First add an append-only optional `session_node_number()` host callback using
  `bbs_host_api_t.size` feature detection; retain handle-only operation on older
  hosts.
- Implement exact/prefix/node recipient resolution with ambiguity reporting.
- Add typed private, directed, action, and AFK events.
- Implement recipient-specific delivery filtering.
- Add optional private-message bell.
- Add per-session rate limiting and neutral block failures.
- Persist ignore/block relationships by user ID.
- Add public, directed, private, and system rendering tests.

Exit gate:

- Private messages never leak into room history or other sessions.
- Ignore affects public/action visibility; block affects private delivery.
- AFK responses do not create reply loops.
- Ambiguous handles cause no delivery.
- Multi-session integration suite passes.

### Milestone 4 — persistence and preferences

Purpose: make MTS behavior durable across restart.

Commands:

- `/settings`
- `/settings <key> <value>` for the approved FEATURELIST settings
- `/settings reset`

Work:

- Add `mts_store` schema version 1 and migration harness.
- Add `mts.conf` loader.
- Persist configured rooms, preferences, ignore/block, visits, and optional
  bounded history.
- Load default room safely, falling back to Lobby when inaccessible.
- Add history retention and expired-policy cleanup tasks.
- Keep persistence work outside topology locks.

Exit gate:

- Preferences and relationships survive plugin/BBS restart.
- Schema creation and upgrade are transactional.
- Corrupt or unwritable storage fails clearly without partial startup.
- ANSI-off output remains usable.

### Milestone 5 — user rooms and moderation

Purpose: add safely governed user-created spaces.

Commands:

- `/create`, `/rename`, `/topic <text>`, `/public`, `/private`
- `/invite`, `/uninvite`, `/transfer`
- `/kick`, `/ban`, `/unban`, `/bans`
- `/mute`, `/unmute`

Work:

- Implement owner/moderator role and one-room-per-user default.
- Add invitations, room bans, room mutes, and expiration parsing.
- Delete empty temporary rooms and preserve permanent rooms.
- Move kicked/closed-room users to Lobby safely.
- Add structured moderation audit persistence.
- Enforce confirmation and reason rules for destructive commands.

Exit gate:

- Private-room membership cannot be bypassed by rename, reconnect, duplicate
  session, or handle change.
- Moderator scope is limited to the owned room.
- Expired bans/mutes no longer apply and are cleaned up.
- Every moderation change has an audit record.

### Milestone 6 — sysop operations

Purpose: make MTS operable without database edits.

Commands:

- `/announce`
- `/chatlock`
- `/rooms admin`
- `/close`
- `/globalmute`, `/unglobalmute`
- `/globalban`, `/unglobalban`
- `/mitsaudit [count]`

Work:

- Centralize AR-A authorization.
- Add global moderation records and expiry handling.
- Define reconnect behavior for global bans.
- Add clear audit views with bounded pagination.
- Log MTS operational actions through both plugin logging and its structured
  audit store.

Exit gate:

- Non-sysops cannot invoke or alias privileged commands.
- Chat lock blocks new entry but does not strand current users.
- Global announcements have an unspoofable event type.
- Sysop actions are covered by negative authorization tests.

### Milestone 7 — profiles, discovery, history, and actions

Purpose: add the approved social depth after moderation is mature.

Commands:

- `/profile`, `/profile set`, `/profile <user>`
- `/members [prefix]`
- `/recent`
- `/history [count]`
- `/search <text>` when enabled
- `/actions`, `/action <name> [user]`
- Optional `/stats`

Work:

- Add privacy flags and retention/deletion policy.
- Add bounded queries and pagination.
- Implement strict named-action templates and sysop management.
- Ensure ignore/block applies to actions and discovery privacy.
- Avoid popularity rankings in default statistics.

Exit gate:

- Private last-seen/profile settings are enforced in every lookup path.
- Search is bounded and cannot expose private events.
- Action templates cannot inject terminal controls or arbitrary format strings.

### Milestone 8 — optional focused chat and room watch

Purpose: implement P3 only after core MTS is stable.

- `/chat`, `/decline`, and focused one-to-one view.
- Persistent `allow chat requests` preference.
- `/watch`, `/unwatch`, and carefully scoped public-room monitoring.
- Private rooms never leak through watch.
- `/watch all` is sysop-only or disabled by default.

This milestone is optional and should not delay a stable MTS release.

### Milestone 9 — remaining host ABI improvements

Purpose: remove polling and expose missing terminal/session facts.

Design a backward-compatible plugin ABI minor revision adding optional callbacks
at the end of `bbs_host_api_t`:

- Nonblocking input or input-ready polling.
- Wake/signal an active plugin instance.
- Security level.
- ANSI/terminal capability.
- Forced-disconnect lifecycle event.
- Structured audit callback.
- Plugin configuration lookup.

Rules:

- Use the existing `size` field for feature detection.
- Keep ABI major 1 if additions are append-only and old plugins remain binary
  compatible.
- MTS must continue to run in polling mode against the original ABI.
- Node-number addressing is unavailable, with clear help text, when the
  optional callback is absent.
- Add old-host/new-plugin and new-host/old-plugin compatibility tests.

## CMake and packaging work

- Extend `add_mutineer_plugin` or define an MTS-specific target that accepts
  multiple sources.
- Link `chat_plugin` with `Threads::Threads` explicitly rather than relying on
  host symbol visibility.
- Link SQLite when `mts_store.c` lands.
- Add MTS unit targets and labels to CTest.
- Package default MTS configuration and any text/ANSI assets under a stable
  plugin data/config path.
- Preserve `chat_plugin.so` until a deliberate compatibility migration exists.
- Add release validation that loads the packaged plugin and checks descriptor
  identity/version.

## Documentation work

Update with each milestone rather than at the end:

- `plugins/chat/FEATURELIST.md` — capability truth and deferrals.
- `docs/chat-and-social.md` — caller guide and command reference.
- `docs/plugins.md` — MTS architecture and plugin example status.
- `docs/doors-and-scripting.md` — menu invocation and packaging.
- `docs/reference/menu-actions.md` — stable plugin action.
- `menus/main.mnu` — user-facing MTS label at Milestone 2.
- Add `plugins/chat/README.md` when MTS becomes operator-configurable.

Do not describe MTS as a sample plugin after Milestone 1. It becomes a shipped
Mutineer subsystem implemented through the plugin boundary.

## Test matrix

Every milestone must cover:

| Area | Required evidence |
|---|---|
| Parsing | Valid, missing, extra, ambiguous, mixed-case, and control-byte input |
| Identity | Stable user ID, handle changes, prefix ambiguity, duplicate sessions |
| Rooms | Join/leave races, Lobby invariants, occupancy, private access |
| Delivery | Ordering, recipient isolation, overflow, slow readers |
| Safety | Ignore, block, mute, ban, AFK-loop prevention, rate limits |
| Authorization | User, moderator, sysop, and negative/alias paths |
| Persistence | Fresh DB, migration, restart, expiry, rollback, corruption |
| Terminal | ANSI on/off, narrow display, hostile control sequences |
| Lifecycle | Normal exit, EIO, forced disconnect, shutdown with active sessions |
| Concurrency | Repeated parallel join/post/move/leave under sanitizers |

## Definition of the first production release

MTS 2.0 is ready when Milestones 0 through 3 are complete and:

- Public rooms, direct/private messages, actions, AFK, ignore, and block work in
  real concurrent BBS sessions.
- There are no known message-isolation or authorization failures.
- Session cleanup is idempotent under normal exit and connection loss.
- The plugin has bounded memory and explicit overflow behavior.
- All untrusted terminal text is sanitized.
- `ctest -R 'plugin|mts'` and the interactive MTS suite pass.
- User documentation matches the command table.

Persistence, user-created rooms, moderation, profiles, and optional P3 features
ship in later minor releases unless implementation proves small and fully
tested. Do not hold the production foundation hostage to broad feature parity.

## First implementation task

Begin with Milestone 0 only:

1. Add a fake MTS host and direct `chat_plugin.so` test harness.
2. Capture current commands and lifecycle behavior in tests.
3. Reproduce ring wraparound and slow-reader loss explicitly.
4. Define the new internal structs in `mts.h` without switching production
   behavior yet.
5. Get the baseline and skeleton building cleanly before changing delivery.

This gives the refactor a safety net and prevents a large rewrite from silently
breaking plugin loading, terminal I/O, or session teardown.
