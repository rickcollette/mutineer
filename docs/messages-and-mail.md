<!-- generated-by: gsd-doc-writer -->

# Messages and Mail

Mutineer BBS provides threaded message areas, private email, offline QWK mail, and FidoNet netmail/echomail integration. Message operations use two-letter M* (message) and R* (read) commands compatible with classic BBS conventions.

## Message Areas

Message areas are stored in the `message_areas` table with optional conference linkage.

### Area Fields

| Field | Purpose |
|-------|---------|
| `name` | Display name |
| `filename` | Base filename for area storage |
| `acs_read` | ACS required to read |
| `acs_post` | ACS required to post |
| `acs_sysop` | ACS for sysop functions |
| `anon_policy` | Anonymous posting policy (0=No, 1=Yes, 2=Forced, 3=DearAbby, 4=AnyName) |
| `max_msgs` | Maximum messages retained |
| `password` | Optional area password |
| `origin` | Origin line for echomail |

### Conferences

Conferences group message areas. Users join via `joinconf`/`leaveconf` actions or conference editor.

- ACS term `C?` checks if user is in conference letter `?` (A=0)
- ACS term `J#` checks conference membership by ID in DB
- `conference_membership` table tracks joins

## Message Commands (M*)

Dispatched by `handle_msg_command()` in `src/msg_cmds.c`:

| Command | Handler | Description |
|---------|---------|-------------|
| `MA` | `cmd_msg_area_change` | Change current message area |
| `MG` | `cmd_msg_area_list` | List available message areas |
| `MN` | `cmd_msg_new_scan` | Scan for new messages |
| `MP` | `cmd_msg_post` | Post new message |
| `MR` | `cmd_msg_read` | Read message by number |
| `MS` | `cmd_msg_search` | Search messages |
| `MW` | `cmd_msg_write_email` | Write private email |
| `MY` | `cmd_msg_your_scan` | Scan messages addressed to you |
| `MZ` | `cmd_msg_toggle_scan_areas` | Toggle per-area scan flags |

Enter commands at the `:` prompt in message mode or via menu data fields (e.g., `MZ` in `menus/message.mnu`).

## Read Commands (R*)

| Command | Handler | Description |
|---------|---------|-------------|
| `RA` | `cmd_msg_read` | Read all (alias) |
| `RC` | `cmd_msg_continuous_read` | Continuous read mode |
| `RE` | `cmd_msg_reply` | Reply to message |
| `RJ` | `cmd_msg_jump_reply` | Jump to message and reply |
| `RL` | `cmd_msg_list` | List messages in area |
| `RM` | `cmd_msg_edit` | Edit own message |
| `RN` | `cmd_msg_read_new` | Read new messages |
| `RP` | `cmd_msg_read_private` | Read private mail |
| `RQ` | `cmd_msg_quick_scan` | Quick scan headers |
| `RT` | `cmd_msg_thread_view` | Thread tree view |
| `RV` | `cmd_msg_view_attachment` | View/download attachment |
| `RY` | `cmd_msg_your_messages` | Your messages list |

Full reference: [Message Commands](reference/message-commands.md).

## Posting Flow

1. User enters area (menu or `MA`)
2. ACS check on `acs_post`
3. AC flag check (`AC_RPOST`, `AC_RPOSTAN` for anonymous)
4. Subject/body collected via line editor or full-screen editor (`fsedit_edit`)
5. Optional signature/tagline appended if user toggles enabled
6. `db_message_post()` inserts record with threading (`reply_to`, `thread_root`)
7. Stats updated; echomail queue populated if linked

### Full-Screen Editor

Toggle via `togglefse` menu action. Shared between message and email composition (`fsedit_edit()` in `msg_cmds.c`).

### Signatures and Taglines

| Action | Description |
|--------|-------------|
| `setsignature` | Set user signature text |
| `settagline` | Set user tagline text |

User record fields: `signature`, `tagline`, `use_signature`, `use_tagline`.

## Private Email

Private mail uses the message system with `to_user` set:

- `MW` — compose email to handle
- `RP` — read private messages
- `MY` / `RY` — scan/list messages to you

Email restricted by AC flag `AC_REMAIL`. Security level may limit daily email via `email_allow` in security levels.

## QWK Offline Mail

QWK support in `src/qwk.c` enables offline mail packets for remote readers.

### Database Tables

| Table | Purpose |
|-------|---------|
| `qwk_hubs` | Network hub definitions |
| `qwk_area_links` | Map local areas to hub conferences |
| `qwk_packet_queue` | Pending import/export packets |
| `mail_packets` | Generated packet records per user |

### User Flow

Menu action `netmail` provides QWK download/upload. CLI tool `mutineer-qwkgen` generates packets for batch/cron use.

### QWK Commands (in session)

| Command | Handler | Description |
|---------|---------|-------------|
| QWK download | `cmd_qwk_download` | Generate and download QWK packet |
| QWK upload | `cmd_qwk_upload` | Upload REP packet |

User field `last_qwk` tracks last packet date.

## FidoNet

FidoNet support in `src/fido_netmail.c`:

### Tables

| Table | Purpose |
|-------|---------|
| `fido_akas` | Up to 20 AKA addresses (zone:net/node.point) |
| `fido_echolinks` | Echo tag to message area mapping |
| `fido_netmail` | Outbound netmail queue |
| `fido_echomail_queue` | Echomail export queue |

### Sysop Tools

| Action | Description |
|--------|-------------|
| `fidoeditor` | Manage AKAs and echolinks |
| `fidosend` | Send pending netmail |

CLI: `mutineer-netmail-export` exports the netmail queue to BSO format.

## Message Attributes

Messages carry `attr` and `net_attr` bitsets for local and FidoNet semantics (private, sent, deleted, netmail, etc.). See `include/bbs_msg_defs.h`.

## Drafts

Incomplete compositions saved to `drafts` table on disconnect:

| Field | Purpose |
|-------|---------|
| `user_id` | Owner |
| `area_id` | Target area |
| `to_user_id` / `to_name` | Email recipient |
| `subject` / `body` | Draft content |

## Scan Flags

`MZ` toggles per-user area scan participation in `user_msg_scan_areas`. Disabled areas are skipped during `MN`/`RN` scans.

## Restrictions

AC flags blocking message activity:

| Flag | ACS | Effect |
|------|-----|--------|
| `AC_RMSG` | `RM` | Block message areas entirely |
| `AC_RPOST` | `RP` | Block posting |
| `AC_RPOSTAN` | `R*` | Block anonymous posting |
| `AC_REMAIL` | `RE` | Block email |

## PLANK Integration

When PLANK areas map to local message areas (`plank_areas.local_area_id`), posts may generate PLANK objects for store-and-forward sync. See [PLANK Networking](networking-plank.md).

## Maintenance

| Tool | Purpose |
|------|---------|
| `mutineer-msgpack` | Pack/prune old messages |
| `mutineer-maint` | Database maintenance |

In-BBS: `maintenance` action → message pack options via `maint.c`.

## Related Documentation

- [Reference: Message Commands](reference/message-commands.md)
- [Reference: Database](reference/database.md)
- [Menus and UI](menus-and-ui.md)
- [Sysop Guide](sysop-guide.md)
