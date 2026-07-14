# Mutineer BBS Functional Specification

This document describes every functional capability currently implemented in the Mutineer BBS system based on comprehensive code analysis.

---

## 1. SYSTEM ARCHITECTURE

### 1.1 Core Components
- **Main Program** (`main.c`): Entry point, signal handling, startup orchestration
- **Session Management** (`session.c`): User sessions, authentication, action handling
- **Network Listener** (`net_listener.c`): TCP/IP socket listener, connection handling
- **Configuration** (`config.c`): Config file parsing and defaults
- **Database** (`db.c`): SQLite database abstraction layer
- **Startup** (`startup.c`): Sanity checks, directory creation, database initialization

### 1.2 Data Storage
| Component | Storage |
|-----------|---------|
| Configuration | `conf/mutineer.conf` (INI-style) |
| Database | SQLite (`data/mutineer.db`) |
| Schema | `sql/schema.sql` |
| Art/ANSI | `art/` directory |
| Menus | `menus/` directory (`.mnu` files) |
| Logs | `logs/` directory |
| Files | `data/files/` directory |
| Dropfiles | `data/dropfiles/` directory |

### 1.3 Command Line Parameters
| Parameter | Function |
|-----------|----------|
| `--config <path>` | Specify configuration file path |
| `--help` | Display usage information |

### 1.4 Signal Handling
- `SIGINT/SIGTERM`: Graceful shutdown
- `SIGUSR1`: Trigger broadcast file check
- `SIGPIPE`: Ignored (prevents crash on client disconnect)

---

## 2. USER MANAGEMENT

### 2.1 User Registration (`session.c`)
- **New User Flow**: Handle/password/confirmation prompts
- **Required Fields**: Handle, password, city/state, email
- **Optional Fields**: Social media link, message to SysOp
- **Password Requirements**: Minimum 4 characters, confirmation required
- **Default Credits**: Configurable via `default_credits` and `default_file_points`
- **Welcome Letter**: Automatic welcome message sent to new users

### 2.1.1 Welcome Letter System (`session.c`)
**Configuration (mutineer.conf):**
- `welcome_letter_enabled` - Enable/disable (0/1)
- `welcome_letter_file` - Path to welcome text file
- `welcome_letter_from` - Sender name (e.g., "Sysop")

**Features:**
- Reads welcome text from configurable file
- Sends SMW notification to new user
- Default file: `art/welcome.txt`

### 2.1.2 Guest Account System (`session.c`)
**Configuration (mutineer.conf):**
- `guest_enabled` - Enable/disable guest access (0/1)
- `guest_handle` - Handle to trigger guest login (default: "GUEST")
- `guest_level_id` - Security level for guests

**Guest Login Flow:**
1. User enters guest handle (e.g., "GUEST")
2. System prompts for name, location, referral
3. Temporary user record created with limited access
4. 30-minute time limit enforced

**Features:**
- No password required
- Quick registration info collected
- Limited access based on guest security level
- No persistent user record created

### 2.2 User Record Fields (DbUser)
```
- id (integer, auto-increment)
- handle (64 chars)
- real_name (64 chars)
- pw_hash (128 chars, PBKDF2 or Argon2)
- email (64 chars)
- phone (20 chars)
- street (64 chars)
- city_state (64 chars)
- zip_code (16 chars)
- caller_id (32 chars)
- forgot_pw_answer (64 chars)
- sex (M/F/U)
- birth_date (YYYY-MM-DD)
- security_level_id (FK to security_levels)
- level (computed from security level)
- dsl (download security level)
- time_limit_min
- flags (AR flags A-Z bitset)
- ac_flags (restriction/privilege flags)
- status_flags (locked, deleted, expert, etc.)
- credits, file_points
- on_today, illegal
- def_arc_type, color_scheme, user_start_menu
- first_on, t_time_on, last_qwk
- uploads, downloads, uk, dk
- logged_on, msg_post, email_sent, feedback
- timebank, timebank_add
- dl_k_today, dl_today
- usr_def_str1/2/3 (custom fields)
- social_link, sysop_msg, note
- locked_file
- last_login_at, expires_at
- smw (short message waiting)
- dl_ratio_num/den, post_ratio_num/den
```

### 2.3 User Flags
**AR Flags (A-Z bitset):**
- 26 flags (A through Z) for access control

**AC (Restriction) Flags:**
- `AC_RLOGON` (L) - Restricted from logging on
- `AC_RCHAT` (C) - Restricted from chat
- `AC_RVALIDATE` (V) - Restricted from validation
- `AC_RUSERLIST` (U) - Restricted from user list
- `AC_RAMSG` (A) - Restricted from auto-message
- `AC_RPOSTAN` (*) - Restricted from anonymous posting
- `AC_RPOST` (P) - Restricted from posting
- `AC_REMAIL` (E) - Restricted from email
- `AC_RVOTING` (K) - Restricted from voting
- `AC_RMSG` (M) - Restricted from messages
- `AC_FNODLRATIO` (1) - No download ratio enforced
- `AC_FNOPOSTRATIO` (2) - No post ratio enforced
- `AC_FNOCREDITS` (3) - No credits required
- `AC_FNODELETION` (4) - Cannot be deleted

**Status Flags:**
- `STATUS_LOCKED` - Account locked
- `STATUS_DELETED` - Account deleted
- `STATUS_EXPERT` - Expert mode
- `STATUS_ANSI` - ANSI graphics
- `STATUS_PAUSE` - Pause at page end
- `STATUS_HOTKEYS` - Hot keys enabled
- `STATUS_CLEAR` - Clear screen between menus
- `STATUS_NOCHAT` - Not available for chat
- `STATUS_NOPAGE` - Cannot be paged

### 2.4 User Authentication (`session.c`)
- Handle/password login
- Password hashing with PBKDF2-SHA256 (200,000 iterations)
- Optional Argon2id support (if compiled with HAVE_ARGON2)
- Automatic password hash upgrade on login
- Login throttling (configurable attempts/window)
- Last login timestamp tracking

### 2.5 User Editor (`session.c` - useredit action)
- Full field editing for sysops
- AR/AC/Status flag manipulation (hex input)
- Security level, DSL, time limit adjustment
- Credits, file points, timebank editing
- Expiration date management
- Sysop note field
- Audit logging of edits

---

## 3. MESSAGE SYSTEM

### 3.1 Message Areas (`msg_cmds.c`, `session.c`)
**Area Record (DbMsgArea):**
- id, name, filename
- acs, acs_read, acs_post, acs_sysop
- anon_policy, flags, password, origin
- max_msgs

**Area Operations:**
- `MA` - Change message area
- `MG` - List message areas (with message counts)
- Area ACS enforcement

### 3.2 Message Operations
| Command | Function |
|---------|----------|
| `MA` | Change message area |
| `MG` | List message areas |
| `MR` | Read messages in current area |
| `MP` | Post new message |
| `MN` | New message scan (since last login) |
| `MS` | Search messages (subject/body) |
| `MW` | Write email (private mail) |
| `MY` | Your Scan - messages addressed to you |
| `RC` | Continuous read (no prompts) |
| `RE` | Reply to message (with quoting) |
| `RJ` | Jump to reply (follow reply chain) |
| `RL` | Message list (paginated) |
| `RM` | Edit own message |
| `RN` | Read new messages |
| `RP` | Read private mail |
| `RQ` | Quick scan (subject lines only) |
| `RT` | Thread view (indented tree) |
| `RV` | View/download attachment |
| `RY` | Your posted messages |

### 3.3 Message Record (DbMessage)
```
- id, area_id, user_id, to_user, reply_to, thread_root
- user_handle, from_name, to_name
- subject (80 chars), body (2048 chars)
- created_at, attr, net_attr
- file_attached, origin
```

### 3.4 Message Features
- Threaded message display (depth calculation)
- Reply with automatic quoting ("> " prefix)
- Private mail (to_user field)
- SMW (Short Message Waiting) notification
- Message search across all areas
- New message scan since last login
- Post count tracking

### 3.5 Full-Screen Editor (`session.c`)
- `fsedit` action for visual message composition
- Line-based editing with cursor movement (arrow keys)
- Insert/overwrite modes
- Keyboard shortcuts: Ctrl-S (save), Ctrl-A (abort), Ctrl-Y (delete line), Ctrl-N (insert line)
- Automatic line wrapping at 78 characters
- Maximum 50 lines per message
- Preview before posting
- Direct integration with message posting

### 3.6 Short Messages (SMW)
- `smw` action for reading/sending short messages
- Real-time notification to online users
- Database table `short_messages` for persistent storage
- Functions: `db_smw_send()`, `db_smw_list()`, `db_smw_count()`, `db_smw_mark_read()`, `db_smw_delete()`
- Automatic SMW count update on send
- Mark messages as read when viewed

### 3.8 Conferences (`db.c`, `session.c`)
**Conference Record (DbConference):**
- id, key, name, description
- acs, flags

**Conference Operations:**
| Action | Function |
|--------|----------|
| `joinconf` | Join a conference |
| `leaveconf` | Leave a conference |
| `conflist` | List user's conferences |

**Database Functions:**
- `db_conference_list()` - List all conferences
- `db_conference_get()` - Get conference by ID
- `db_conference_add()` - Create new conference
- `db_conference_update()` - Update conference
- `db_conference_delete()` - Delete conference

**Membership Functions:**
- `db_conf_is_member()` - Check if user is member
- `db_conf_join()` - Add user to conference
- `db_conf_leave()` - Remove user from conference
- `db_conf_list_user()` - List user's conference IDs

**ACS Integration:**
- `J<n>` ACS condition checks conference membership
- Example: `J1` - user must be member of conference 1

---

## 4. FILE SYSTEM

### 4.1 File Areas (`file_cmds.c`, `session.c`)
**Area Record (DbFileArea):**
- id, name, path
- acs_list, acs_download, acs_upload, acs_sysop
- password, max_files
- archive_type, sort_type
- show_uploader, check_dupes, free_files, flags

**Area Flags:**
- `FA_FLAG_CDROM` - CD-ROM area
- `FA_FLAG_FREEFILES` - All files free
- `FA_FLAG_NOCOUNT` - Don't count towards ratio
- `FA_FLAG_NOTIME` - No time deducted
- `FA_FLAG_PRIVATE` - Private uploads only
- `FA_FLAG_SLOW` - Slow media

### 4.2 File Operations
| Command | Function |
|---------|----------|
| `FA` | Change file area |
| `FG` | List file areas |
| `FL` | List files in current area |
| `FD` | Download file |
| `FU` | Upload file |
| `FE` | View extended description |
| `FB` | Batch download queue |
| `FC` | Clear batch queue |
| `FF` | Find file (search descriptions) |
| `FN` | New files scan |
| `FV` | View archive contents |
| `FZ` | Zippy search (filename only) |
| `FR` | Raw directory listing |
| `FX` | Expert mode toggle |

### 4.3 File Record (DbFileRec)
```
- id, area_id
- filename (128 chars), desc (256 chars)
- extended_desc (2048 chars), file_id_diz (2048 chars)
- size_bytes, uploaded_at, uploaded_by, uploader
- sha256 (65 chars)
- file_points, download_count, owner_credit, flags
```

**File Flags:**
- `FILE_FLAG_NOTVAL` - Not validated
- `FILE_FLAG_OFFLINE` - Offline
- `FILE_FLAG_UNHIDDEN` - Visible to unvalidated users
- `FILE_FLAG_RESUME` - Allow resume
- `FILE_FLAG_NOTIME` - No time deducted
- `FILE_FLAG_FREE` - Free download

### 4.4 File Transfer Features
- Credit/ratio enforcement (with exemption flags)
- Batch download queue (up to 32 files)
- Protocol selection (Zmodem, Ymodem, Xmodem)
- External protocol execution via fork/exec
- SHA256 duplicate detection on upload
- FILE_ID.DIZ extraction
- Upload validation queue
- File point rewards for uploads

### 4.5 Archive Viewing (`file_cmds.c`)
Supported formats (via external tools):
- ZIP (`unzip -l`)
- TAR (`tar -tvf`)
- TGZ/TAR.GZ (`tar -tzvf`)
- GZ (`gzip -l`)
- RAR (`unrar l`)
- 7Z (`7z l`)
- ARJ (`arj l`)
- LZH/LHA (`lha l`)
- BZ2 (`bzip2 -l`)
- XZ (`xz -l`)

---

## 5. MENU SYSTEM

### 5.1 Menu Structure (`menu.c`)
**Menu Record:**
```
- name (from filename)
- title (120 chars)
- prompt (120 chars)
- art_file (display file)
- fallback (fallback menu)
- flags (menu behavior)
- gen_cols (1-4 columns)
- items[] (menu items array)
- count (number of items)
```

**Menu Flags:**
- `MENU_FLAG_CLRSCR_BEFORE` - Clear screen before display
- `MENU_FLAG_DONT_CENTER` - Don't center title
- `MENU_FLAG_NO_MENU_TITLE` - Hide title
- `MENU_FLAG_FORCE_PAUSE` - Force pause
- `MENU_FLAG_AUTO_TIME` - Show time remaining
- `MENU_FLAG_NO_MENU_PROMPT` - Hide prompt
- `MENU_FLAG_USE_LIGHTBAR` - Lightbar navigation
- `MENU_FLAG_HOTKEYS` - Hot key mode

### 5.2 Menu Item Record
```
- key (single char) or key_str (multi-char)
- label (120 chars)
- action (64 chars)
- data (64 chars)
- acs (64 chars)
- password (32 chars)
- flags (CMD_FLAG_*)
```

**Command Flags:**
- `CMD_FLAG_HIDDEN` - Always hidden
- `CMD_FLAG_UNHIDDEN` - Always visible
- `CMD_FLAG_PASSWORD` - Requires password
- `CMD_FLAG_SYSOP_LOG` - Log to sysop log

### 5.3 Menu File Format (`.mnu`)
```
# Comment
@TITLE Menu Title
@FLAGS clrscr,hotkeys
@PROMPT Selection: 
@ART menu.ans
@FALLBACK main
@COLS 2

KEY|Label|Action|Data|ACS|CmdFlags|Password
```

### 5.4 Built-in Actions (`session.c`)
| Action | Function |
|--------|----------|
| `who` | Show online users |
| `wall` | Send wall message |
| `whisper` | Send private message to node |
| `messages` | Message area browser |
| `files` | File area browser |
| `chat` | Multi-node chat |
| `linechat` | Line chat with node |
| `splitchat` | Split-screen chat |
| `bulletins` | View bulletins |
| `oneliners` | One-liner wall |
| `page` | Page sysop |
| `useredit` | User editor (sysop) |
| `doors` | Door games |
| `vote` | Voting booth |
| `timebank` | Time bank |
| `areaadmin` | Message area admin |
| `fileadmin` | File area admin |
| `netmail` | QWK packet queue |
| `batchrun` | Execute batch queue |
| `maintenance` | System maintenance |
| `help` | Help screen |
| `logout` | Log off |

---

## 6. ACCESS CONTROL SYSTEM (ACS)

### 6.1 ACS Parser (`acs.c`)
Recursive-descent parser with precedence: `!` > `&` > `|`

**Operators:**
- `|` - OR
- `&` - AND (implicit between conditions)
- `!` - NOT
- `()` - Grouping

### 6.2 ACS Conditions
| Code | Meaning |
|------|---------|
| `S<n>` | Security level >= n |
| `D<n>` | Download security level >= n |
| `F<x>` | Has AR flag x (A-Z) |
| `R<x>` | Has AC restriction flag x |
| `A<n>` | Age >= n |
| `B<n>` | Baud >= n*100 (always true for telnet) |
| `C<x>` | In conference x (A-Z) |
| `G<x>` | Gender is x (M/F) |
| `H<n>` | Current hour = n |
| `J<n>` | Member of conference n |
| `N<n>` | Node number = n |
| `P<n>` | Credits >= n |
| `T<n>` | Time remaining >= n |
| `U<n>` | User number = n |
| `V` | User is validated (not expired) |
| `W<n>` | Day of week = n (0=Sun) |
| `X<n>` | Days until expiration <= n |
| `Z` | Meets post ratio |

**Legacy Support:**
- `SLnn` or `Lnn` - Security level >= nn
- `+X` - AR flag X set
- `ARABC` - All listed AR flags set
- `C>=N`, `R>=N`, `T>=N` - Comparison operators

---

## 7. MULTINODE SUPPORT

### 7.1 Node Status (`session.c`, `db.c`)
**Node Record (DbNode):**
```
- node_num
- user_id
- handle
- status (logging_in, online, chat, kicked, etc.)
- activity
- ip
```

### 7.2 Online Registry
- `online_add()` - Register session
- `online_remove()` - Unregister session
- `online_list()` - List online users
- `online_broadcast()` - Send to all users
- `online_get_node()` - Get session by node number

### 7.3 Inter-Node Communication
- Wall messages (broadcast to all)
- Whisper (private to specific node)
- Page sysop
- Node status display

### 7.4 Multi-Login Prevention
- Configurable via `allow_multi_login` config option (default: 0 = blocked)
- Checks `nodes` table for existing session with same `user_id`
- Displays `MULTILOG` art file when duplicate login detected
- Shows which node the user is already logged in on

---

## 8. CHAT SYSTEM

### 8.1 Multi-Node Chat (`session.c`)
- Channel-based chat (channels 1-9)
- Ring buffer for chat history (128 messages)
- Real-time message display
- `/quit` command to exit

### 8.2 Line Chat (`session.c`)
- One-on-one chat with specific node
- Channel 100+ for private line chat

### 8.3 Split Chat (`session.c`, `chat.c`)
- Emulated split-screen via rapid refresh
- Channel 200+ for split chat

### 8.4 Teleconference System (`chat.c`)
- Up to 10 rooms, 20 users per room
- Room creation with topic
- Private rooms with password
- Moderator controls
- `/list`, `/join`, `/create`, `/who`, `/quit` commands

---

## 9. DOORS/EXTERNAL PROGRAMS

### 9.1 Door Drop Files (`doors.c`)
| Format | File | Lines |
|--------|------|-------|
| DOOR.SYS | `DOOR.SYS` | 51 lines |
| DOOR32.SYS | `DOOR32.SYS` | 11 lines |
| DORINFO | `DORINFO1.DEF` | 12 lines |
| CHAIN.TXT | `CHAIN.TXT` | 30 lines (WWIV) |
| PCBOARD.SYS | `PCBOARD.SYS` | 128 bytes binary |
| CALLINFO.BBS | `CALLINFO.BBS` | 25 lines |
| SFDOORS.DAT | `SFDOORS.DAT` | 13 lines |

### 9.2 Door Execution
- All drop files written to `data/dropfiles/<doorname>/`
- External command execution uses supervised argv templates with shell metacharacter rejection, process-group timeout handling, and disconnect-aware cancellation.
- Return code logging

### 9.3 Protocol Execution (`doors.c`)
- Fork/exec model for file transfers
- Socket redirection to stdin/stdout/stderr
- Command substitution (`%f` for filepath)
- Exit code handling

---

## 10. QWK OFFLINE MAIL

### 10.1 QWK Packet Generation (`qwk.c`)
Files generated:
- `CONTROL.DAT` - Conference list, system info
- `MESSAGES.DAT` - Message data (128-byte blocks)
- `DOOR.ID` - Door identification
- `###.NDX` - Conference index files (per conference)
- `PERSONAL.NDX` - Personal message index

### 10.2 QWK Message Format
- 128-byte header block
- Variable-length body blocks
- Line ending conversion (LF → 227)

### 10.3 NDX Index Files (`qwk.c`)
- 5-byte records per message
- 4-byte MSB floating-point block offset
- 1-byte conference number
- Enables fast message lookup in offline readers

### 10.4 REP Packet Import (`qwk.c`)
- ZIP extraction to temp directory
- `MUTINEER.MSG` parsing
- Message posting with ACS checking
- Conference number mapping

### 10.5 QWK Commands
- `!D` - Download QWK packet
- `!U` - Upload REP packet

---

## 11. EVENTS/SCHEDULER

### 11.1 Event System (`scheduler.c`)
**Event Record (DbEvent):**
```
- id, name
- schedule (cron-ish string)
- command
- next_run
- event_type (scheduled, logon, permission)
- acs (for permission events)
- warning_min (pre-event warning minutes)
- enabled (0/1)
```

### 11.2 Scheduler Thread
- Background thread polling events
- Configurable tick interval (`scheduler_tick_sec`)
- External command execution
- Next run time calculation
- Pre-event warning broadcasts

### 11.3 Schedule Formats
- `daily@HH:MM` - Daily at specific time
- `weekly:Day@HH:MM` - Weekly on specific day (Sun, Mon, Tue, Wed, Thu, Fri, Sat)
- `monthly:DD@HH:MM` - Monthly on specific day of month
- `every:N` - Every N seconds

### 11.4 Event Types
| Type | Description |
|------|-------------|
| `scheduled` | Time-based events (default) |
| `logon` | Run when user logs in |
| `permission` | ACS-triggered events |

### 11.5 Logon Events
- `scheduler_run_logon_events()` called after successful login
- Environment variables set: `BBS_USER_ID`, `BBS_USER_HANDLE`
- Commands can use these to customize per-user actions

### 11.6 Pre-Event Warnings
- `warning_min` field specifies minutes before event
- Online users receive broadcast notification
- Useful for maintenance windows

---

## 12. VOTING SYSTEM

### 12.1 Voting Booth (`session.c`)
**Vote Record (DbVote):**
```
- id, title, closes_at
```

**Choice Record (DbVoteChoice):**
```
- id, vote_id, label
```

### 12.2 Voting Features
- List open votes
- Display choices
- Cast vote (one per user per topic)
- AC_RVOTING restriction check

### 12.3 Vote Results (VR)
**Action:** `voteresults`

**Display:**
- List all votes with IDs
- Select vote to view results
- Shows each choice with:
  - Vote count
  - Percentage of total
  - ASCII bar graph visualization
- Total votes cast
- Closing date if set

**Functions:**
- `db_vote_results()` - Get vote counts per choice
- `db_vote_total()` - Get total votes for a topic

---

## 13. STATISTICS & LOGGING

### 13.1 System Statistics (`db.c`)
**Daily Stats (DbDailyStats):**
```
- date, calls, posts, emails
- newusers, feedback
- uploads, downloads
- ul_kb, dl_kb
- minutes, errors
```

**System Totals (DbSystemTotals):**
```
- total_calls, total_posts
- total_uploads, total_downloads
- total_usage, days_online
- total_users
```

### 13.2 Logging (`log.c`)
- `log_info()` - Informational messages
- `log_warn()` - Warning messages
- `log_error()` - Error messages
- `log_audit()` - Audit trail (user/action/detail)
- `log_trap()` - Trap file logging

### 13.3 History (`db.c`)
- `db_history_record()` - Save daily stats
- `db_history_list()` - Retrieve history
- `db_days_online()` - Days since first call

### 13.4 Call History (`db.c`, `session.c`)
**Call Record (DbCallHistory):**
- id, user_id, handle
- node_num, login_at, logout_at
- duration_min, ip_address

**Functions:**
- `db_call_log_start()` - Log session start
- `db_call_log_end()` - Log session end with duration
- `db_call_history_list()` - Get recent callers

**Action:**
- `lastcallers` - Display last 20 callers (OH command)
- Shows handle, node, login time, duration

---

## 14. TERMINAL EMULATION

### 14.1 Telnet Protocol (`telnet.c`)
- IAC command handling
- WILL/WONT/DO/DONT negotiation
- NAWS (window size) support
- TTYPE (terminal type) support
- SGA (suppress go-ahead)
- ECHO control

### 14.2 ANSI Support
- ANSI detection prompt at login
- Color codes (0-15)
- Cursor positioning
- Screen clearing

---

## 15. MCI CODES

### 15.1 MCI System (`mci.c`)
**Color Codes:**
- `^0-^9` - User color scheme colors
- `~L#` - Set foreground (0-15)
- `~B#` - Set background (0-7)
- `~K#` - Blink (0/1)
- `~RS` - Reset all attributes

**User MCI Codes:**
| Code | Meaning |
|------|---------|
| `~UN` | User handle |
| `~RN` | Real name |
| `~U#` | User ID |
| `~AG` | User age |
| `~BD` | Birth date |
| `~SX` | Sex (M/F/U) |
| `~CT` | City/state |
| `~ST` | Street |
| `~ZP` | Zip code |
| `~PH` | Phone |
| `~FO` | First on date |
| `~LO` | Last on date |
| `~TT` | Total time on |
| `~TL` | Time left |
| `~TB` | Time bank |
| `~SL` | Security level |
| `~DL` | Downloads |
| `~UL` | Uploads |
| `~DK` | Download KB |
| `~UK` | Upload KB |
| `~MP` | Messages posted |
| `~ES` | Emails sent |
| `~FB` | Feedback |
| `~CR` | Credits |
| `~FP` | File points |
| `~LG` | Total logons |
| `~NN` | Node number |
| `~AR` | AR flags list |

**System MCI Codes:**
| Code | Meaning |
|------|---------|
| `~BN` | BBS name |
| `~SN` | Sysop name |
| `~BP` | BBS phone |
| `~TC` | Total calls |
| `~NU` | Number of users |
| `~NF` | Number of files |
| `~NM` | Number of messages |
| `~NO` | Number online |
| `~VR` | Version |
| `~DA` | Date |
| `~TM` | Time |
| `~AN` | Area name |

**Legacy % Codes:**
- `%UN`, `%TI`, `%NN`, `%DA`, `%TM`, `%PO`, `%MT`, `%FT`, `%TL`, `%CR`, `%FP`, `%SL`, `%MA`, `%FA`, `%NO`, `%AR`
- `%NL` - Newline (skip)
- `%PE` - Pause
- `%?ACS{then|else}` - Conditional

---

## 16. SYSOP TOOLS

### 16.1 User Editor (`session.c`)
- Full field editing
- Flag manipulation
- Level/credit adjustment

### 16.2 Area Administration (`session.c`)
- Message area add/delete/ACS
- File area add/delete/ACS

### 16.3 Menu Editor (`session.c`)
- `menueditor` action for visual menu editing
- List all menu files from `menus/` directory
- Create new menu files with default template
- Edit menu title, prompt, and flags
- Add new menu items (key, label, action, ACS)
- Save menu changes back to `.mnu` file format

### 16.4 Protocol Editor (*X) (`session.c`)
**Action:** `protocoleditor`

**Features:**
- List all file transfer protocols
- Add new protocols (name, direction, command)
- Edit existing protocols
- Delete protocols
- Toggle active/inactive status

**Protocol Fields:**
- Name (e.g., "Zmodem", "Xmodem")
- Direction (up/down/both)
- Command (external program path with arguments)
- Active flag

**Functions:**
- `db_protocol_add()` - Add new protocol
- `db_protocol_update()` - Update protocol
- `db_protocol_delete()` - Delete protocol
- `db_protocol_get()` - Get protocol by ID

### 16.5 Conference Editor (*R) (`session.c`)
**Action:** `confeditor`

**Features:**
- List all conferences with ID, key, name, ACS
- Add new conferences (key, name, description, ACS)
- Edit existing conferences
- Delete conferences (removes all memberships)

**Conference Fields:**
- Key (short identifier)
- Name (display name)
- Description
- ACS (access control string)
- Flags

**Functions:**
- `db_conference_add()` - Create conference
- `db_conference_update()` - Update conference
- `db_conference_delete()` - Delete conference and memberships
- `db_conference_get()` - Get conference by ID
- `db_conference_list()` - List all conferences

### 16.6 Maintenance (`maint.c`)
- User pack (remove deleted)
- Message pack (remove old)
- File pack (remove orphans)
- Rebuild indexes
- Database vacuum
- System statistics

---

## 17. WFC (WAITING FOR CALL) SCREEN

### 17.1 WFC Display (`wfc.c`)
- Real-time clock display
- Today's stats panel
- System averages panel
- System totals panel
- Other info panel (disk, mail, errors)
- 64-node matrix display (4x16 grid)
- Status line
- Command menu

### 17.2 WFC Commands
| Key | Function |
|-----|----------|
| `S` | System config |
| `F` | File base |
| `C` | Callers list |
| `!` | Validate user |
| `@` | Inspect node |
| `Q` | Quit |
| `U` | User editor |
| `B` | Message base |
| `L` | Logs |
| `Z` | History |
| `N` | Nodes list |
| `D` | Config-gated supervised shell command |
| `E` | Events |
| `W` | Write mail |
| `R` | Read |
| `K` | Kick node |
| `b` | Broadcast |
| `Space` | Local logon |

### 17.3 WFC Features
- Screen blanking after idle
- Node status colors (Inactive/Logging/Active/SysOp)
- Node inspection with kick/chat options
- Broadcast to all users
- History display
- Log viewing

---

## 18. SECURITY

### 18.1 Password Hashing (`hash.c`)
- PBKDF2-SHA256 (200,000 iterations)
- Argon2id (optional, if compiled)
- Automatic hash upgrade on login
- Base64 encoding

### 18.2 Login Throttling (`session.c`)
- Configurable max attempts
- Configurable window (seconds)
- IP and handle tracking

### 18.2.1 Password Expiration (`session.c`)
**Configuration (mutineer.conf):**
- `password_expire_days` - Days until password expires (0=disabled)

**Features:**
- Tracks password change date in `pw_changed_at` field
- Checks password age on login
- Forces password change if expired
- Updates timestamp when password changed

**Functions:**
- `db_user_set_pw_with_timestamp()` - Set password and update timestamp
- `db_user_pw_age_days()` - Get days since password change

**Flow:**
1. User logs in successfully
2. System checks password age
3. If expired, user must change password
4. New password confirmed and saved
5. Timestamp updated

### 18.3 Password Recovery (`session.c`)
**Security Question System:**
- User sets security question and answer via `setsecurityq` action
- Answer is hashed (same as password)
- On failed login, user can answer security question
- Correct answer allows immediate password reset

**Functions:**
- `db_user_set_security_question()` - Set question and hashed answer
- `db_user_get_security_question()` - Retrieve question and answer hash

**Fields in users table:**
- `forgot_pw_question` - Security question text
- `forgot_pw_answer` - Hashed answer

### 18.4 Security Levels (`db.c`)
**Security Level Record (DbSecurityLevel):**
```
- id, name, level
- time_limit_min, call_allow
- dl_one_day, dl_k_one_day
- download_ratio_num/den
- post_ratio_num/den
- ul_dl_ratio_num/den
- post_call_ratio
- email_allow, vote_allow, anon_allow
- flags
```

### 18.4 Validation Levels (`db.c`)
**Validation Level Record (DbValidationLevel):**
```
- id, key (A-Z)
- description, user_msg
- new_sl, new_dsl, new_menu
- expiration, expire_to
- new_fp, new_credit
- soft_ar, soft_ac
- new_ar, new_ac
```

### 18.6 Subscription System (`db.c`, `session.c`)
**Subscription Type Record (DbSubscriptionType):**
- id, name, days
- security_level_id (level while subscribed)
- expired_level_id (level after expiry)
- price (in credits)
- description

**User Subscription Record (DbUserSubscription):**
- id, user_id, subscription_type_id
- started_at, expires_at
- status (active, expired, cancelled)

**Actions:**
| Action | Description |
|--------|-------------|
| `subscriptioneditor` | Sysop: manage subscription types |
| `subscribe` | User: purchase subscription |

**Functions:**
- `db_subscription_type_list()` - List subscription types
- `db_subscription_type_add()` - Create subscription type
- `db_subscription_type_get()` - Get type by ID
- `db_user_subscribe()` - Subscribe user to plan
- `db_user_subscription_get()` - Get user's active subscription
- `db_subscription_check_expired()` - Process expired subscriptions
- `db_user_set_expires()` - Set user expiration date

**Features:**
- Automatic level upgrade on subscription
- Automatic level demotion on expiry
- Credit-based purchasing
- Multiple subscription tiers

---

## 19. MISCELLANEOUS FEATURES

### 19.1 Auto-Message (`session.c`, `db.c`)
- System-wide message display at login
- User can set new auto-message

### 19.2 One-Liners (`session.c`)
- Short user messages (80 chars)
- Display at login
- Add new one-liners

### 19.3 Bulletins (`session.c`)
- Numbered bulletin display
- ACS per bulletin
- MCI expansion in title/body

### 19.4 Time Bank (`session.c`)
- Deposit session time
- Withdraw stored time
- Balance tracking

### 19.5 Broadcast System (`session.c`)
- File-based broadcast (`data/broadcast.txt`)
- SIGUSR1 trigger
- Immediate delivery to all online users

---

## 20. CONFIGURATION

### 20.1 Config File Options (`config.c`)
| Option | Default | Description |
|--------|---------|-------------|
| `bind` | 0.0.0.0 | Bind address |
| `port` | 2929 | Listen port |
| `db_path` | data/mutineer.db | Database path |
| `menu_main` | menus/main.mnu | Main menu file |
| `idle_timeout_sec` | 600 | Idle timeout |
| `motd` | art/motd.ans | MOTD file |
| `data_path` | data | Data directory |
| `logs_path` | logs/mutineer.log | Log file |
| `art_path` | art | Art directory |
| `session_time_limit_min` | 60 | Session time limit |
| `wfc_enabled` | 1 | Enable WFC screen |
| `wfc_refresh_ms` | 1000 | WFC refresh rate |
| `wfc_blank_sec` | 300 | WFC blank timeout |
| `wfc_fg_color` | 11 | WFC foreground |
| `wfc_bg_color` | 0 | WFC background |
| `scheduler_enabled` | 1 | Enable scheduler |
| `scheduler_tick_sec` | 30 | Scheduler interval |
| `login_window_sec` | 120 | Login throttle window |
| `login_max_attempts` | 5 | Max login attempts |
| `password_upgrade` | 1 | Auto-upgrade hashes |
| `default_credits` | 5000 | New user credits |
| `default_file_points` | 0 | New user file points |
| `doors_path` | doors | Doors directory |
| `dropfile_path` | data/dropfiles | Dropfile directory |
| `protocol_path` | conf/protocols.conf | Protocol config |

---

## 21. UTILITIES

### 21.1 Utility Functions (`util.c`)
- `str_trim()` - Trim whitespace
- `str_starts_with()` - String prefix check
- `fd_write_all()` - Write all bytes to fd
- `fd_readline()` - Read line from fd
- `file_read_all()` - Read entire file
- `path_join()` - Join path components
- `file_copy()` - Copy file

---

## 22. DATABASE SCHEMA

### 22.1 Core Tables
- `users` - User accounts
- `security_levels` - Security level definitions
- `validation_levels` - Validation key definitions
- `nodes` - Active node status
- `message_areas` - Message area definitions
- `messages` - Message content
- `file_areas` - File area definitions
- `files` - File records
- `bulletins` - Bulletin posts
- `oneliners` - One-liner wall
- `automsg` - Auto-message
- `stats` - System statistics
- `daily_stats` - Daily statistics
- `history` - Historical statistics
- `system_info` - System configuration
- `votes` - Voting topics
- `vote_choices` - Vote options
- `user_votes` - User vote tracking
- `doors` - Door definitions
- `protocols` - Transfer protocols
- `events` - Scheduled events
- `meta` - Key-value metadata
- `fido_akas` - FidoNet AKA addresses
- `fido_echolinks` - FidoNet echomail area links
- `fido_netmail` - FidoNet netmail queue
- `fido_echomail_queue` - Echomail export queue
- `qwk_hubs` - QWK network hubs
- `qwk_area_links` - QWK area links
- `qwk_packet_queue` - QWK packet queue
- `chat_logs` - Chat message logs

---

## 23. FIDONET/ECHOMAIL SUPPORT

### 23.1 FidoNet Address System (`db.c`, `session.c`)

**Address Format:**
- Zone:Net/Node.Point (e.g., 1:123/456.0)
- Optional domain suffix

**AKA Management (DbFidoAka):**
| Field | Description |
|-------|-------------|
| `zone` | FidoNet zone number |
| `net` | Network number |
| `node` | Node number |
| `point` | Point number (0 for boss nodes) |
| `domain` | Optional domain name |
| `is_primary` | Primary AKA flag |

**Functions:**
- `db_fido_aka_list()` - List all AKAs (up to 20)
- `db_fido_aka_add()` - Add new AKA
- `db_fido_aka_get()` - Get AKA by ID
- `db_fido_aka_get_primary()` - Get primary AKA
- `db_fido_aka_update()` - Update AKA
- `db_fido_aka_delete()` - Delete AKA

**Helper Functions:**
- `fido_format_address()` - Format AKA as string
- `fido_parse_address()` - Parse address string

### 23.2 Echomail Links (`db.c`, `session.c`)

**DbFidoEcholink Structure:**
| Field | Description |
|-------|-------------|
| `area_id` | Linked message area ID |
| `echotag` | FidoNet echo tag (e.g., BBS_CARNIVAL) |
| `aka_id` | AKA to use for this echo |
| `origin` | Custom origin line |
| `high_water` | Last exported message ID |

**Functions:**
- `db_fido_echolink_list()` - List all echolinks
- `db_fido_echolink_add()` - Add new echolink
- `db_fido_echolink_get()` - Get echolink by ID
- `db_fido_echolink_get_by_area()` - Get echolink by area
- `db_fido_echolink_update()` - Update echolink
- `db_fido_echolink_delete()` - Delete echolink
- `db_fido_echolink_update_highwater()` - Update high water mark

### 23.3 Netmail Queue (`db.c`, `session.c`)

**DbFidoNetmail Structure:**
| Field | Description |
|-------|-------------|
| `from_zone/net/node/point` | Sender address |
| `from_name` | Sender name |
| `to_zone/net/node/point` | Recipient address |
| `to_name` | Recipient name |
| `subject` | Message subject |
| `body` | Message body |
| `attr` | NET_ATTR_* flags |
| `status` | pending/sent/failed |

**Functions:**
- `db_fido_netmail_list()` - List netmail by status
- `db_fido_netmail_add()` - Queue new netmail
- `db_fido_netmail_get()` - Get netmail by ID
- `db_fido_netmail_mark_sent()` - Mark as sent
- `db_fido_netmail_delete()` - Delete netmail

### 23.4 Echomail Export Queue (`db.c`)

**Functions:**
- `db_fido_echo_queue_add()` - Queue message for export
- `db_fido_echo_queue_pending()` - Get pending exports
- `db_fido_echo_queue_mark_exported()` - Mark as exported

### 23.5 FidoNet Editor (*F) (`session.c`)

**Action:** `fidoeditor`

**Features:**
- AKA address management (add/edit/delete/set primary)
- Echomail link configuration
- Netmail queue viewing and management

**Access:** Sysop only (+A)

### 23.6 Send Netmail (`session.c`)

**Action:** `fidosend`

**Features:**
- Compose netmail to any FidoNet address
- Uses primary AKA as sender
- Queues for external tosser/scanner

---

## 24. QWK NETWORKING

### 24.1 QWK Network Hubs (`db.c`, `session.c`)

**DbQwkHub Structure:**
| Field | Description |
|-------|-------------|
| `name` | Hub name |
| `bbs_id` | BBS ID for packet naming (8 chars) |
| `call_schedule` | Cron-like schedule for calling |
| `last_call` | Last call timestamp |
| `enabled` | Hub enabled flag |

**Functions:**
- `db_qwk_hub_list()` - List all hubs
- `db_qwk_hub_add()` - Add new hub
- `db_qwk_hub_get()` - Get hub by ID
- `db_qwk_hub_update()` - Update hub
- `db_qwk_hub_delete()` - Delete hub and related data
- `db_qwk_hub_mark_called()` - Update last call timestamp

### 24.2 QWK Area Links (`db.c`, `session.c`)

**DbQwkAreaLink Structure:**
| Field | Description |
|-------|-------------|
| `hub_id` | Parent hub ID |
| `area_id` | Local message area ID |
| `remote_conf` | Conference number on hub |
| `high_water_in` | Last imported message ID |
| `high_water_out` | Last exported message ID |

**Functions:**
- `db_qwk_area_link_list()` - List links for hub
- `db_qwk_area_link_add()` - Add new link
- `db_qwk_area_link_get()` - Get link by ID
- `db_qwk_area_link_update()` - Update link
- `db_qwk_area_link_delete()` - Delete link
- `db_qwk_area_link_update_highwater()` - Update high water marks

### 24.3 QWK Packet Queue (`db.c`, `session.c`)

**DbQwkPacket Structure:**
| Field | Description |
|-------|-------------|
| `hub_id` | Parent hub ID |
| `packet_type` | 'qwk' (incoming) or 'rep' (outgoing) |
| `packet_path` | Path to packet file |
| `status` | pending/processed/failed |

**Functions:**
- `db_qwk_packet_list()` - List packets by hub/status
- `db_qwk_packet_add()` - Queue new packet
- `db_qwk_packet_mark_processed()` - Mark as processed
- `db_qwk_packet_delete()` - Delete packet

### 24.4 QWK Network Editor (*Q) (`session.c`)

**Action:** `qwkneteditor`

**Features:**
- Hub management (add/edit/delete/toggle)
- Area link configuration
- Packet queue management

**Access:** Sysop only (+A)

---

## 25. CHAT LOGGING

### 25.1 Chat Logs Table (`schema.sql`)

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER | Primary key |
| `chat_type` | TEXT | 'split' or 'teleconf' |
| `room_id` | INTEGER | Teleconference room ID (0 for split chat) |
| `from_user` | INTEGER | Sender user ID |
| `from_handle` | TEXT | Sender handle |
| `to_user` | INTEGER | Recipient user ID (split chat only) |
| `to_handle` | TEXT | Recipient handle (split chat only) |
| `message` | TEXT | Chat message content |
| `logged_at` | TEXT | Timestamp |

**Indexes:**
- `idx_chat_logs_type` - By chat type
- `idx_chat_logs_time` - By timestamp (descending)

### 25.2 Chat Logging Functions (`db.c`)

**Functions:**
- `db_chat_log()` - Log a chat message
- `db_chat_log_list()` - Retrieve chat logs by type/room
- `db_chat_log_clear()` - Purge old chat logs (by days)

### 25.3 Logged Chat Types

**Split-Screen Chat (`chat.c`):**
- Logs chat start/end markers
- Logs all messages between two users
- Records both sender and recipient

**Teleconference (`chat.c`):**
- Logs all broadcast messages
- Records room ID and sender
- Initialized via `teleconf_set_db()` at startup

---

*Document generated from comprehensive analysis of Mutineer BBS source code.*
*Source files analyzed: 25 C files, 22 header files*
