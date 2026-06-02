<!-- generated-by: gsd-doc-writer -->

# Message Commands Reference

Two-letter commands for message and email operations. Enter at the `:` prompt in message mode or via menu data fields.

Dispatched by `handle_msg_command()` in `src/msg_cmds.c`. Declarations in `include/bbs_msg_cmds.h`.

## M* Commands (Message)

| Cmd | Function | Description |
|-----|----------|-------------|
| `MA` | `cmd_msg_area_change` | Change current message area by number or name |
| `MG` | `cmd_msg_area_list` | List all accessible message areas |
| `MN` | `cmd_msg_new_scan` | Scan areas for new messages since last visit |
| `MP` | `cmd_msg_post` | Post new message in current area |
| `MR` | `cmd_msg_read` | Read message by number |
| `MS` | `cmd_msg_search` | Search messages by subject/body keyword |
| `MW` | `cmd_msg_write_email` | Compose private email to user |
| `MY` | `cmd_msg_your_scan` | Scan for messages addressed to you |
| `MZ` | `cmd_msg_toggle_scan_areas` | Toggle per-area new-scan participation |

### MA — Area Change

Changes `s->current_msg_area`. Prompts for area number or name. ACS checked against area `acs_read`.

### MG — Area List

Lists areas passing ACS with area ID and name. Respects conference membership.

### MN — New Scan

Scans enabled areas (per `user_msg_scan_areas`) for messages posted since user's last read pointer. Shows count and optional jump-to-read.

### MP — Post

Opens post flow:

1. Check `acs_post` and `AC_RPOST`
2. Prompt subject (or full-screen editor if enabled)
3. Prompt body
4. Append signature/tagline if configured
5. Insert via `db_message_post()`

Supports anonymous posting per area `anon_policy`.

### MR — Read

Read message by number in current area. Displays header, body, and attachment info. Offers reply/forward/delete options.

### MS — Search

Prompts for search string. Searches subject and body in current area or all areas.

### MW — Write Email

Private email composition:

1. Prompt recipient handle
2. Validate user exists
3. Subject and body entry
4. Sets `to_user` on message record

Blocked by `AC_REMAIL`.

### MY — Your Scan

Scans for messages where user is recipient (`to_user` or @mention patterns).

### MZ — Toggle Scan Areas

Interactive toggle of `user_msg_scan_areas.scan_enabled` per area. Disabled areas skipped by `MN`/`RN`.

## R* Commands (Read)

| Cmd | Function | Description |
|-----|----------|-------------|
| `RA` | `cmd_msg_read` | Read all messages (alias for MR sequence) |
| `RC` | `cmd_msg_continuous_read` | Continuous read — advance through messages |
| `RE` | `cmd_msg_reply` | Reply to current/ specified message |
| `RJ` | `cmd_msg_jump_reply` | Jump to message number then reply |
| `RL` | `cmd_msg_list` | List message headers in current area |
| `RM` | `cmd_msg_edit` | Edit own message (if not read-only area) |
| `RN` | `cmd_msg_read_new` | Read all new messages sequentially |
| `RP` | `cmd_msg_read_private` | Read private mail messages |
| `RQ` | `cmd_msg_quick_scan` | Quick header scan without reading bodies |
| `RT` | `cmd_msg_thread_view` | Display message thread tree |
| `RV` | `cmd_msg_view_attachment` | View or download file attachment |
| `RY` | `cmd_msg_your_messages` | List messages authored by or sent to you |

### RC — Continuous Read

Reads messages sequentially without returning to prompt between each. Exit with `Q` or blank.

### RE — Reply

Reply to message:

1. Quote original body with `>` prefix
2. Prompt for reply body
3. Set `reply_to` and `thread_root` links
4. Subject prefixed with `Re:`

### RJ — Jump Reply

Combines jump to message number + reply flow.

### RL — List

Paginated header list: ID, subject, author, date. Respects expert mode display level.

### RM — Edit

Edit user's own message if area policy allows. Sysop may edit any message with `acs_sysop`.

### RN — Read New

Reads all messages marked new since last visit across scan-enabled areas.

### RP — Read Private

Filters messages with private attribute or `to_user` set to current user.

### RQ — Quick Scan

Header-only scan showing subject lines. Faster than full read for high-volume areas.

### RT — Thread View

Displays thread tree with indentation based on `reply_to` chain (up to depth limit).

### RV — View Attachment

If message has `file_attached`, offer view/download of attachment file.

### RY — Your Messages

Lists messages where user is author or named recipient.

## Full-Screen Editor

`fsedit_edit()` shared editor for MP, MW, and RE:

- Arrow key navigation (if terminal supports)
- Line editing commands
- Auto-save draft to `drafts` table on disconnect

Toggle default via `togglefse` menu action.

## QWK Commands

Separate from M*/R* dispatch but related:

| Cmd | Function | Description |
|-----|----------|-------------|
| QWK download | `cmd_qwk_download` | Generate QWK packet for user |
| QWK upload | `cmd_qwk_upload` | Import REP reply packet |

Invoked from `netmail` menu action.

## Error Messages

Unknown commands produce:

```
Unknown message command.
Unknown read command.
```

## Usage Examples

At message area prompt:

```
:MG              List areas
:MA 3            Change to area 3
:MN              Scan for new
:MP              Post message
:MR 42           Read message 42
:RE              Reply to last read
:MW              Write email
:RN              Read all new
:MZ              Toggle scan areas
```

From menu (`menus/message.mnu`):

```
Z|Toggle Scan Areas|messages|MZ|L10
```

## Related Documentation

- [Messages and Mail](../messages-and-mail.md)
- [Reference: File Commands](file-commands.md)
- [Menus and UI](../menus-and-ui.md)
