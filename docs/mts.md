# Mutineer Teleconference System (MTS)

MTS is Mutineer's primary real-time chat system. The stock main-menu `C` entry
loads plugin `com.mutineer.chat` (`chat_plugin.so`). The older `chat`,
`linechat`, and `splitchat` menu actions remain available to custom menus but
are not presented by the stock menu.

MTS provides public and private rooms, room topics and presence, private and
focused conversations, actions, AFK status, ignore/block controls, persistent
preferences and profiles, optional history, room watching, rate limits, room
moderation, and sysop announcements and locking. Type `/help` inside MTS for
the current command list. Sysop operations require AR flag `A`; display names
never grant authority. Accounts carrying restriction flag `AC_RCHAT` cannot
enter MTS.

The canonical generated command and preference reference is
[MTS commands](reference/mts-commands.md).

## Configuration and data

On first initialization MTS creates `data/plugins/com.mutineer.chat/mts.db` and
runs its transactional schema migration. Copy `plugins/chat/mts.conf.example`
to `data/plugins/com.mutineer.chat/mts.conf` to override defaults. Missing
configuration uses bounded safe defaults; unknown keys are logged; invalid
limits prevent the plugin from loading. Configuration is immutable until the
next plugin/process restart.

Room history persistence is disabled by default. When enabled, only room
events are eligible for history; private messages are never stored in the
history table. Ignore/block relationships, preferences, profiles, rooms, and
moderation audit records are persisted by numeric user ID.

## Privacy and moderation

Private messages are delivered to every active MTS presence for the addressed
account. A full private inbox causes an explicit nondelivery event to the
sender; it is never silently overwritten. Public overflow is collapsed into a
skipped-message notice. Watching is limited to public rooms. Operational
announcements use a typed, unspoofable event.

Room owners may moderate their room. Global moderation and `/watch all` are
reserved for sysops. Moderation changes are written to the MTS audit table and,
when host ABI 1.1 is available, to the host audit log.
