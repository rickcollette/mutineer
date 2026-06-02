# Mutineer vs. ORIGINAL_BBS parity tracker

Goal: make the C Mutineer build match the Pascal ORIGINAL_BBS behavior as closely as possible (name stays "Mutineer"; base color theme should be green instead of their blue).

Statuses: ✅ implemented, 🟡 partial/stubbed, ❌ missing, 🎨 theme-only.

## Visual / theme

- 🎨 Base palette: green (Mutineer) vs. blue (Renegade). **Status:** 🟡 (motd + logon/help/sysop/WFC/system art done; file/message/door/voting screens still use inline text).
- 🎨 Color scheme selection. **Status:** ✅ (`pickscheme` action; 8 named schemes; `^0–^9` MCI tokens backed by scheme table).

## Core runtime

- Program name / banner: executable named `mutineer`. **Status:** ✅
- Telnet listener: multi-client thread-per-connection with SGA/ECHO/NAWS/TTYPE. **Status:** ✅
- Multinode state tracking (node table, who list, WFC, per-node upsert). **Status:** ✅
- Config system: key/value loader with full DOSBox/door/chat/page defaults. **Status:** 🟡 (no interactive config editor).
- Event scheduler: cron-ish schedules backed by `events` table; sysop Event Editor (`*E`). **Status:** ✅
- Daily call limit (`max_calls_per_day`), time limit, idle timeout enforced. **Status:** ✅

## Accounts / security

- User records (create/edit, security levels, time limits, ratios). **Status:** ✅
- Password hashing/throttling (PBKDF2 + Argon2 upgrade; per-IP throttle). **Status:** ✅
- Password expiry + forced change on login (`PWCHANGE` art). **Status:** ✅
- Multi-login prevention. **Status:** ✅
- Guest account. **Status:** ✅
- Subscription management (types, active check, expiry). **Status:** ✅
- Sysop F-key shortcuts (remote). **Status:** ✅ (F1=who, F2=broadcast, F3=kick, F4=stats, F8=twit, F10=chat; VT100/ANSI/xterm sequences).

## ACS system

- `S#`, `D#`, `F?`, `R?`, `A#`, `B#`, `G?`, `H#`, `N#`, `P#`, `T#`, `U#`, `V`, `W#`, `X#`. **Status:** ✅
- `C#` (calls/logins ≥ n), `C?` (conference letter). **Status:** ✅
- `E#` (active subscription of type #). **Status:** ✅
- `J#` (conference member), `PC` (post/call ratio), `DR` (download ratio), `Z`. **Status:** ✅
- Legacy: `SL#`, `L#`, `+X`, `AR`, `C>=N`, `R>=N`, `T>=N`. **Status:** ✅

## Menus / UI

- External `.mnu` files driving commands. **Status:** ✅
- Menu Editor (`menueditor` sysop action). **Status:** ✅
- Full menu tree (WFC, logon, sysop, file/message/door/chat/vote areas). **Status:** 🟡 (deeper per-area trees pending).
- MCI/ANSI token expansion (`%XX`, `~XX`, `^0–^9`, `~L#`, `~B#`, `~RS`). **Status:** ✅

## Messaging

- Message bases (areas, read/post/reply, quoting, thread view). **Status:** ✅
- CC recipients, taglines, signatures, FSEditor, MZ scan flags. **Status:** ✅
- Mailbox capacity limits, draft auto-save. **Status:** ✅
- Email / local mail + SMW. **Status:** ✅
- QWK packets (NDX, NEWFILES.DAT, per-user max-msgs). **Status:** ✅
- FidoNet netmail outbound export. **Status:** 🟡 (export implemented; tosser integration pending).

## File areas

- File bases, upload/download, descriptions, ratios, credits. **Status:** ✅
- FP (scan date), FK (batch remove), FJ (batch upload), FT (archive test), FQ (archive extract). **Status:** ✅
- `*7` Validate Files sysop command. **Status:** ✅
- Archive viewing/conversion hooks. **Status:** ❌ (stub).
- Batch transfer, protocols (Zmodem/Xmodem via external commands). **Status:** 🟡

## Doors / external programs

- Native doors: dropfile generation (DOOR.SYS, DORINFO1.DEF, DOOR32.SYS, CHAIN.TXT, PCBOARD.SYS, CALLINFO.BBS, SFDOORS.DAT); sandboxed workdir; ACS. **Status:** ✅
- DOSBox DOS door runner: JSON manifest, per-launch isolated runtime tree, serial nullmodem socket inheritance, supervision + timeout, cleanup. **Status:** ✅

## Chat

- Multi-node channel chat, line chat, split-screen chat. **Status:** 🟡 (split-screen UI is rapid-refresh; proper two-panel ANSI pending).
- Paging sysop with max-page limit and email fallback. **Status:** ✅
- Per-session chat logging to file (`chat_log_path`). **Status:** ✅

## System ops / maintenance

- Maintenance (VACUUM, message purge, schema migration). **Status:** ✅
- Time bank, vote booths (Vote Editor `*V`), bulletins, automsg. **Status:** ✅
- Conference Editor (`*R`), Protocol Editor (`*X`). **Status:** ✅
- Last callers display, subscription management. **Status:** ✅
- Logging/audit (audit + trap logs). **Status:** 🟡 (more detail pending).

## Display files / art

- Missing system display files (`NOACCESS`, `2MANYCAL`, `NOTLEFTA`, `NOCREDTS`, `PWCHANGE`). **Status:** ✅ (`.ans` + `.asc` created; wired into session at access denied / time limit / credits / password expiry / daily limit).
- Full green-themed ANSI art pack. **Status:** 🟡 (system display files done; file/message/door/voting screens still inline).

## Data storage

- SQLite schema: users/nodes/messages/files/votes/doors/events/protocols/stats/timebank/subscriptions/drafts/echolinks. **Status:** ✅ (auto-migration via `db_apply_core_migrations`).

## Networking / protocols

- Telnet-only; no FOSSIL/modem emulation. **Status:** ✅
- PLANK offline networking (bundle export/import, store, identity, dedupe). **Status:** ✅
- FTN/netmail routing. **Status:** 🟡 (outbound netmail export; full tosser integration pending).

## Immediate next candidates

1. Complete green ANSI art pack (file area, message area, door, voting screens).
2. Improve split-screen chat UI (proper two-panel ANSI).
3. FTN echomail tosser integration.
4. Archive/virus-scan hook wiring.
