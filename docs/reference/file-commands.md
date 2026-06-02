<!-- generated-by: gsd-doc-writer -->

# File Commands Reference

Two-letter `F*` commands for file area operations. Enter at the `:` prompt in file mode.

Dispatched by `handle_file_command()` in `src/file_cmds.c`. Declarations in `include/bbs_file_cmds.h`.

## Command Table

| Cmd | Function | Description |
|-----|----------|-------------|
| `FA` | `cmd_file_area_change` | Change current file area |
| `FB` | `cmd_file_batch_download` | Download all files in batch queue |
| `FC` | `cmd_file_batch_clear` | Clear batch download queue |
| `FD` | `cmd_file_download` | Download file by number |
| `FE` | `cmd_file_extended` | Show extended description |
| `FF` | `cmd_file_find` | Find files by name/description |
| `FG` | `cmd_file_area_list` | List accessible file areas |
| `FJ` | `cmd_file_batch_upload` | Batch upload multiple files |
| `FK` | `cmd_file_batch_remove` | Remove entry from batch queue |
| `FL` | `cmd_file_list` | List files in current area |
| `FN` | `cmd_file_new_scan` | List files uploaded since scan date |
| `FP` | `cmd_file_set_scan_date` | Set new-file scan cutoff date |
| `FQ` | `cmd_file_archive_extract` | Extract archive to temp directory |
| `FR` | `cmd_file_raw_dir` | Raw directory listing (expert) |
| `FT` | `cmd_file_archive_test` | Test archive integrity |
| `FU` | `cmd_file_upload` | Upload file to current area |
| `FV` | `cmd_file_view_archive` | View archive contents listing |
| `FX` | `cmd_file_expert_toggle` | Toggle expert mode display |
| `FZ` | `cmd_file_zippy_search` | Fast wildcard filename search |

## Area Commands

### FA — Area Change

Changes `s->current_file_area`. Validates ACS on `acs_list` before switching.

### FG — Area List

Lists file areas with ID, name, and path. Filters by ACS.

## Listing Commands

### FL — File List

Paginated file listing in current area:

```
#123 FILENAME.ZIP     12345 bytes by Uploader
  Short description here
```

Sorted per area `sort_type` (name/date/size).

### FR — Raw Directory

Expert-mode raw listing without formatting. Requires expert mode or sysop.

### FN — New Scan

Lists files uploaded after user's scan date (`FP` sets this). Shows `[NEW]` marker.

### FE — Extended Description

Displays `extended_desc` and `file_id_diz` fields for specified file number.

## Download Commands

### FD — Download

Download file by number:

1. Validate file exists and ACS on `acs_download`
2. Check credits (cost ≈ KB + 1) unless free
3. Check download ratio unless `AC_FNODLRATIO`
4. Check daily limits from security level
5. Launch transfer protocol or raw send
6. Update download stats

### FB — Batch Download

Download all files queued in session batch queue. Processes sequentially with credit checks per file.

## Upload Commands

### FU — Upload

Upload to current area:

1. ACS check on `acs_upload`
2. Protocol receive or filename prompt
3. Description prompt
4. SHA-256 hash and duplicate detection
5. Insert file record; may enter upload queue if validation required

### FJ — Batch Upload

Upload multiple files in one session. Prompts for each file sequentially.

## Batch Queue Commands

Session maintains in-memory batch queue (max size defined in session struct).

| Cmd | Action |
|-----|--------|
| Queue | Select file number during FL or at prompt |
| `FK` | Remove specific queue entry by index |
| `FC` | Clear entire queue |
| `FB` | Download all queued |

Menu action `batchrun` triggers batch download from main menu.

## Search Commands

### FF — Find

Prompts for search string. Matches filename and description in current area.

### FZ — Zippy Search

Wildcard search (supports `*` and `?` patterns). Faster than FF for pattern matching.

## Archive Commands

Requires external tools (zip, tar, unrar, 7z) on PATH.

### FV — View Archive

Lists contents of archive file without extracting.

### FT — Archive Test

Tests archive integrity. Reports errors if corrupted.

### FQ — Archive Extract

Extracts archive to temporary directory for browsing. Does not add extracted files to area.

Supported formats depend on installed tools and area `archive_type`.

## Configuration Commands

### FP — Set Scan Date

Sets user's new-file scan cutoff to specified date or "today". Used by `FN` to determine new files.

### FX — Expert Toggle

Toggles `STATUS_EXPERT` on user. Expert mode shows additional detail in listings and enables `FR`.

## Credit and Ratio

Downloads deduct credits unless:

- File has `FILE_FLAG_FREE`
- Area has `FA_FLAG_FREEFILES`
- User has `AC_FNOCREDITS`

Ratio enforced via security level and ACS `DR` unless `AC_FNODLRATIO`.

## Error Messages

```
Unknown file command.
Access denied.
Not enough credits.
File missing.
Upload failed (cannot store file).
```

Named art `NOCREDTS` may display on credit failure.

## Usage Examples

```
:FG              List file areas
:FA 2            Change to area 2
:FL              List files
:FD 15           Download file #15
:FU              Upload file
:FF ZIP          Find files matching "ZIP"
:FN              Show new files
:FB              Download batch queue
:FC              Clear batch queue
:FV 20           View archive contents of #20
:FT 20           Test archive #20
:FX              Toggle expert mode
```

## Related Documentation

- [Files and Protocols](../files-and-protocols.md)
- [Reference: Message Commands](message-commands.md)
- [Configuration](../configuration.md)
