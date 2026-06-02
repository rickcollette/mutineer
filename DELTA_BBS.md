# Delta: Renegade BBS Features Missing from Mutineer

This document identifies features present in the Renegade BBS (FUNCTIONAL_BBS.md) that are **not yet implemented** in Mutineer (FUNCTIONAL_MUTINEER.md).

**Architecture Note**: Mutineer is a modern, multiuser telnet BBS designed from the ground up for concurrent TCP/IP connections. Unlike the original Renegade BBS which was designed for single-user-per-node dialup operation, Mutineer uses a threaded architecture where each connection spawns a session thread, all sharing a common SQLite database and in-memory state. This fundamentally changes how many features are implemented.

**Terminal Support**: Mutineer supports ASCII and ANSI terminal modes only. No RIP, Avatar, or other graphics protocols are planned.

---

## SUMMARY

| Category | Implemented | Missing | Coverage |
|----------|-------------|---------|----------|
| System Architecture | High | Low | ~95% |
| User Management | High | Low | ~95% |
| Message System | High | Low | ~90% |
| File System | High | Low | ~90% |
| Menu System | High | Low | ~95% |
| ACS System | High | None | ~100% |
| Multinode | High | Low | ~95% |
| Chat System | High | Low | ~85% |
| Doors | High | Low | ~95% |
| QWK Mail | High | Low | ~90% |
| Events | High | Low | ~90% |
| Voting | High | None | ~95% |
| Statistics | Medium | Medium | ~75% |
| Terminal | High | Low | ~90% |
| Sysop Tools | High | Low | ~95% |
| Miscellaneous | Medium | Low | ~80% |

---

## ARCHITECTURE DIFFERENCES

### Mutineer Modern Architecture (Implemented)
| Feature | Status | Notes |
|---------|--------|-------|
| TCP/IP telnet listener | ✓ | Native socket listener, no fossil driver |
| Threaded sessions | ✓ | pthread per connection, up to 256 concurrent |
| Shared database | ✓ | SQLite with proper locking |
| Dynamic node assignment | ✓ | Nodes assigned from pool on connect |
| In-memory session registry | ✓ | Real-time online user tracking |
| Inter-node messaging | ✓ | Wall, whisper, broadcast via shared memory |
| Real-time chat | ✓ | Channel-based with ring buffer |
| Signal-based events | ✓ | SIGUSR1 for broadcasts, SIGTERM for shutdown |

### Legacy Features NOT Applicable
| Feature | Reason |
|---------|--------|
| COM port settings | Telnet-only, no serial |
| Modem init/hangup strings | No modem support |
| Baud rate detection | All connections are TCP/IP |
| Fossil driver | Native sockets instead |
| EMS/XMS memory | Modern memory model |
| DOS errorlevels | Unix process model |
| Node-specific directories | Shared database model |
| DOOR.SYS baud fields | Set to 38400 for compatibility |
| RIP graphics | Not supported |
| Avatar graphics | Not supported |

---

## 1. SYSTEM ARCHITECTURE

### 1.1 Standalone Utility Tools (Implemented)

Instead of command-line flags on the main BBS binary, Mutineer provides standalone utilities that can be run from the command line, cron, or the BBS scheduler:

| Tool | Function | Status |
|------|----------|--------|
| `mutineer-qwkgen` | Generate QWK packets for a user | ✓ Implemented |
| `mutineer-msgpack` | Pack/purge old messages | ✓ Implemented |
| `mutineer-userpack` | Pack/purge deleted/inactive users | ✓ Implemented |
| `mutineer-filepack` | Remove orphaned file records | ✓ Implemented |
| `mutineer-stats` | Display system statistics (text/JSON) | ✓ Implemented |
| `mutineer-maint` | Database maintenance (vacuum, reindex, backup) | ✓ Implemented |

**Usage Examples:**
```bash
# Generate QWK packet for user
mutineer-qwkgen -v username

# Delete messages older than 90 days
mutineer-msgpack -d 90 -v

# Remove deleted users and those inactive > 1 year
mutineer-userpack -d -i 365 -v

# Check for orphaned file records (dry run)
mutineer-filepack -n -v

# Get stats in JSON format
mutineer-stats -j

# Backup database
mutineer-maint backup -o /backup/bbs.db
```

**Scheduler Integration:**
These tools can be called from the BBS event scheduler:
```
# In events table
name: nightly_msgpack
schedule: daily@03:00
command: /path/to/bin/mutineer-msgpack -d 90
```

### 1.2 Missing Data Files
| File | Purpose |
|------|---------|
| `SHORTMSG.DAT` | Short inter-user messages (implement as DB table) |
| `SCHEME.DAT` | Color scheme definitions (implement as DB or config) |

**Note**: Most Renegade data files are replaced by SQLite tables in Mutineer.

---

## 2. USER MANAGEMENT

### 2.1 Missing Registration Features
| Feature | Description |
|---------|-------------|
| New User Password | System-wide password for new registrations |
| New User Letter | Welcome email from template |
| New User Application | Feedback requirement to SysOp |

### 2.2 Missing User Record Fields
| Field | Description |
|-------|-------------|
| QWK preferences | Detailed QWK configuration per user |
| Conference membership | User's conference subscriptions |
| Voting responses | User's vote history per topic |

**Note**: Most user fields are already implemented in Mutineer's DbUser structure.

### 2.3 Authentication Features

All major authentication features are implemented:

| Feature | Status |
|---------|--------|
| Birthdate verification | 🟡 Field stored; periodic check not enforced |
| Subscription system | ✓ `user_subscriptions` table, expiry, E# ACS condition |
| Password change enforcement | ✓ `password_expire_days` config; `PWCHANGE` art shown |
| Multi-login prevention | ✓ `allow_multi_login` config; `multilog.ans` shown |
| Guest account | ✓ `guest_enabled` + `guest_handle` config |
| Daily call limit | ✓ `max_calls_per_day` config; `2MANYCAL` art shown |

---

## 3. MESSAGE SYSTEM

### 3.1 Missing Area Types
| Type | Description |
|------|-------------|
| Echo (type 1) | FidoNet echomail |
| QWK (type 3) | QWK networked areas |

### 3.2 Missing Area Flags
| Flag | Description |
|------|-------------|
| `MARealName` | Force real names |
| `MAQuote` | Allow quote/tagline |
| `MAAddTear` | Add tear/origin lines |
| `MAInternet` | Internet message area |

### 3.3 Missing Message Operations
| Command | Function |
|---------|----------|
| `MY` | Your Scan - messages addressed to you |
| `MZ` | Toggle Scan Flags - set areas for new scan |
| Edit Message | Modify own messages |

### 3.4 Missing Reading Commands
| Command | Function |
|---------|----------|
| `RC` | Continuous read |
| `RQ` | Quick scan (subjects only) |
| `RL` | List messages |
| `RT` | Thread view |
| `RV` | View/download attachment |
| `RJ` | Jump to reply |

### 3.5 Missing Email Features
| Feature | Description |
|---------|-------------|
| Carbon copies | CC recipients |
| Mailbox capacity limits | Max messages per user |

### 3.6 Missing Editor Features
| Feature | Description |
|---------|-------------|
| Full-screen editor | FSEditor flag |
| Tagline/signature support | Automatic signatures |
| Auto-save on disconnect | Save draft on carrier loss |

### 3.7 Missing FidoNet/Echomail
| Feature | Description |
|---------|-------------|
| Echomail import/export | Full FidoNet support |
| Netmail support | Point-to-point mail |
| Origin line customization | Per-area origins |

---

## 4. FILE SYSTEM

### 4.1 Missing File Operations
| Command | Function |
|---------|----------|
| `FP` | Set new scan date |

### 4.2 Missing Batch Operations
| Command | Function |
|---------|----------|
| `BR` | Remove from batch |
| `BU` | Execute batch upload |

### 4.3 Missing Archive Operations
| Command | Function |
|---------|----------|
| `AT` | Test archive integrity |
| `AE` | Extract to temp |

### 4.4 Missing Protocol Features
| Feature | Description |
|---------|-------------|
| `ProtIsResume` | Resume support flag |
| Bi-directional transfers | Simultaneous up/down |

---

## 5. MENU SYSTEM

### 5.1 Missing Menu Flags
| Flag | Description |
|------|-------------|
| `EVERYTIME` | Execute every menu display |

### 5.2 Missing Menu Commands
| Command | Function |
|---------|----------|
| `-Q` | Display questionnaire |
| `$+` | Add credits |
| `$-` | Add debit |
| `OA` | Auto-validation |
| `OF` | Change AR flags |
| `OG` | Change AC flags |
| `OH` | All callers list |
| `VA` | Add vote topic |
| `VL` | List vote topics |
| `VR` | View vote results |
| `*E` | Event editor |
| `*V` | Vote editor |
| `*R` | Conference editor |
| `*X` | Protocol editor |
| `*#` | Menu editor |
| `*7` | Validate files |

---

## 6. ACCESS CONTROL SYSTEM

### 6.1 ACS Conditions

All major ACS conditions are now implemented:

| Code | Meaning | Status |
|------|---------|--------|
| `P<n>` | Posts >= n | ✓ Implemented (`P#`) |
| `C<n>` | Calls >= n | ✓ Implemented (`C#`) |
| `E<n>` | Has active subscription of type n | ✓ Implemented (`E#`) |
| `J<n>` | In conference x | ✓ Implemented |
| `Q<n>` | Credit >= n (use `P<n>`) | ✓ via `P#` |
| `PC` | Post/call ratio met | ✓ Implemented |
| `DR` | Download ratio met | ✓ Implemented |

---

## 7. MULTINODE SUPPORT

### 7.1 Implemented Features (Modern Architecture)
| Feature | Status | Notes |
|---------|--------|-------|
| Dynamic node pool | ✓ | Up to 256 concurrent nodes |
| Real-time user list | ✓ | `online_list()` function |
| Inter-node broadcast | ✓ | `online_broadcast()` function |
| Node-to-node whisper | ✓ | `online_get_node()` + direct write |
| Wall messages | ✓ | Broadcast to all online users |
| Node status tracking | ✓ | In-memory session registry |
| WFC node matrix | ✓ | 64-node visual display |

### 7.2 Missing Inter-Node Features
| Feature | Description |
|---------|-------------|
| Short messages (SMW) | Persistent one-line inter-user messages |
| Multi-login prevention | Block same user on multiple nodes |
| Invited/Booted/Forget arrays | User interaction tracking |
| Node activity display | Show what each node is doing |

---

## 8. CHAT SYSTEM

### 8.1 Implemented Features
| Feature | Status | Notes |
|---------|--------|-------|
| Multi-node chat | ✓ | Channel-based, ring buffer |
| Line chat | ✓ | One-on-one with specific node |
| Split-screen chat | ✓ | Emulated via rapid refresh |
| Teleconference rooms | ✓ | Up to 10 rooms, 20 users each |
| Room moderation | ✓ | Private rooms, passwords |

### 8.2 Missing Chat Features
| Feature | Description |
|---------|-------------|
| Chat file logging | Separate per user option |
| Chat time added to free time | Don't count chat against time |
| Maximum chat attempts | Limit pages per session |
| Mail option if unavailable | Offer email if SysOp away |

---

## 9. DOORS/EXTERNAL PROGRAMS

### 9.1 Implemented Drop File Formats
| Format | File | Status |
|--------|------|--------|
| DOOR.SYS | `DOOR.SYS` | ✓ |
| DOOR32.SYS | `DOOR32.SYS` | ✓ |
| DORINFO | `DORINFO1.DEF` | ✓ |
| CHAIN.TXT | `CHAIN.TXT` | ✓ |
| PCBOARD.SYS | `PCBOARD.SYS` | ✓ |
| CALLINFO.BBS | `CALLINFO.BBS` | ✓ |
| SFDOORS.DAT | `SFDOORS.DAT` | ✓ |

### 9.2 Door Execution Model
Mutineer uses fork/exec with socket redirection for door execution, which is more robust than the DOS shell-out model. Doors receive the telnet socket directly.

---

## 10. QWK OFFLINE MAIL

### 10.1 Implemented Features
| Feature | Status |
|---------|--------|
| CONTROL.DAT generation | ✓ |
| MESSAGES.DAT generation | ✓ |
| DOOR.ID generation | ✓ |
| REP packet import | ✓ |

### 10.2 Missing QWK Files
| File | Description |
|------|-------------|
| `*.NDX` | Conference indexes |
| `NEWFILES.DAT` | New files list |

### 10.3 Missing QWK Configuration
| Feature | Description |
|---------|-------------|
| Max messages per area | Limit per conference |
| Include new files scan | Add file list to packet |

---

## 11. EVENTS SYSTEM

### 11.1 Implemented Features
| Feature | Status |
|---------|--------|
| Daily scheduled events | ✓ |
| Interval events | ✓ |
| External command execution | ✓ |
| Background scheduler thread | ✓ |

### 11.2 Missing Event Features
| Feature | Description |
|---------|-------------|
| Day of week events | Run on specific days |
| Monthly events | Run on day of month |
| Pre-event warning | Notify users before event |
| Logon events | Run on user login |
| Permission events | ACS-based event triggers |

---

## 12. VOTING SYSTEM

### 12.1 Implemented Features
| Feature | Status |
|---------|--------|
| Vote topics | ✓ |
| Vote choices | ✓ |
| User vote tracking | ✓ |
| AC_RVOTING restriction | ✓ |

### 12.2 Missing Voting Features
| Feature | Description |
|---------|-------------|
| ACS for adding choices | Control who can add options |
| Results display | Show vote percentages |

---

## 13. STATISTICS & LOGGING

### 13.1 Implemented Features
| Feature | Status |
|---------|--------|
| Daily statistics | ✓ |
| System totals | ✓ |
| Audit logging | ✓ |
| Trap file logging | ✓ |
| History tracking | ✓ |

### 13.2 Missing Logging Features
| Feature | Description |
|---------|-------------|
| CHAT files | Chat session logs |
| ERROR.LOG | Runtime errors with context |

### 13.3 Missing Last Callers Features
| Feature | Description |
|---------|-------------|
| Dedicated last callers display | Show recent callers |
| Session statistics | Per-session stats |

---

## 14. TERMINAL EMULATION

### 14.1 Implemented Features
| Feature | Status |
|---------|--------|
| Telnet protocol | ✓ |
| NAWS (window size) | ✓ |
| TTYPE negotiation | ✓ |
| ASCII mode | ✓ |
| ANSI detection | ✓ |
| ANSI color codes | ✓ |

### 14.2 Missing MCI Features
| Feature | Description |
|---------|-------------|
| Full cursor control | Arbitrary positioning |

---

## 15. SYSOP TOOLS

### 15.1 Implemented Features
| Feature | Status |
|---------|--------|
| User editor | ✓ |
| Area administration | ✓ |
| Maintenance tools | ✓ |
| WFC screen | ✓ |
| Node inspection | ✓ |
| Broadcast system | ✓ |

### 15.2 Missing Editors
| Editor | Description |
|--------|-------------|
| Menu Editor | Visual menu editing |
| Protocol Editor | Protocol configuration |
| Conference Editor | Conference management |

---

## 16. NETWORK SUPPORT

### 16.1 Missing FidoNet Features
| Feature | Description |
|---------|-------------|
| Echomail areas | Full FidoNet echomail |
| Netmail | Point-to-point mail |
| Multiple AKAs | Up to 20 addresses |

### 16.2 Missing QWK Networking
| Feature | Description |
|---------|-------------|
| QWK packet exchange | Network QWK support |

---

## 17. SECURITY

### 17.1 Implemented Features
| Feature | Status |
|---------|--------|
| PBKDF2-SHA256 hashing | ✓ |
| Argon2id (optional) | ✓ |
| Login throttling | ✓ |
| Security levels | ✓ |
| Validation levels | ✓ |
| ACS system | ✓ |

### 17.2 Missing Security Features
| Feature | Description |
|---------|-------------|
| Multi-login prevention | Block duplicate logins |
| Password expiration | Force change after N days |
| Security question recovery | Forgot password system |

---

## 18. DISPLAY FILES

### 18.1 Missing System Display Files
| File | When Displayed |
|------|----------------|
| `NOACCESS` | Menu access denied |
| `2MANYCAL` | Too many calls today |
| `NOTLEFTA` | No time left |
| `NOCREDTS` | Insufficient credits |
| `PWCHANGE` | Password change required |
| `MULTILOG` | Already logged in elsewhere |

---

## 19. GUEST ACCOUNT SYSTEM

### 19.1 Missing Guest Features
| Feature | Description |
|---------|-------------|
| Configurable guest user | Designated guest account |
| Quick registration | Name, location, referral |
| Limited access | Validation level for guests |

---

## 20. FORGOT PASSWORD SYSTEM

### 20.1 Missing Password Recovery
| Feature | Description |
|---------|-------------|
| Security question | Configurable question |
| User-defined answer | Stored answer |
| Triggered after max attempts | Auto-prompt on failure |
| Password reset | Allow reset on correct answer |

---

## 21. SUBSCRIPTION SYSTEM

### 21.1 Missing Subscription Features
| Feature | Description |
|---------|-------------|
| Automatic level demotion | Demote on expiry |
| Configurable expiration key | Validation key on expiry |

---

## 22. UTILITY PROGRAMS

### 22.1 Missing Utilities
| Utility | Description |
|---------|-------------|
| RENEMAIL | Echomail processor |

---

## IMPLEMENTATION CHECKLIST

All features listed above are required for full Renegade compatibility. The following is a prioritized implementation order:

### Phase 1: Core User Experience
1. Multi-login prevention
2. Short messages (SMW)
3. Your Scan (MY) - messages addressed to user
4. File attachments (RV)
5. Menu Editor

### Phase 2: Message System Completion
1. Full-screen editor
2. Thread view (RT)
3. Continuous read (RC)
4. Quick scan (RQ)
5. Message list (RL)
6. Jump to reply (RJ)
7. Edit own messages
8. Carbon copies
9. Tagline/signature support

### Phase 3: System Administration
1. Conference system
2. Last callers display
3. Event scheduling (day of week, monthly)
4. Vote results display
5. Protocol editor
6. Conference editor

### Phase 4: User Management
1. Password recovery system
2. Subscription management
3. Guest account system
4. New user welcome letter
5. Password expiration

### Phase 5: Network & Advanced
1. FidoNet/Echomail support
2. QWK networking
3. QWK NDX files
4. Chat logging

---

## NOTES ON MODERN IMPLEMENTATION

### Database vs Files
Mutineer uses SQLite for all persistent data instead of Renegade's flat files. This provides:
- ACID transactions
- Concurrent access safety
- Simpler backup/restore
- Query capabilities

### Threading Model
Each connection gets its own thread with isolated session state. Shared state (online users, chat) uses mutex-protected data structures.

### Socket I/O
Direct socket I/O with telnet protocol handling replaces the DOS FOSSIL driver model. This is more efficient and allows for proper non-blocking I/O.

### Signal Handling
Unix signals (SIGTERM, SIGUSR1) replace DOS interrupt-based event handling. This integrates better with modern process management (systemd, etc.).

### Terminal Support
ASCII and ANSI only. No RIP, Avatar, or other legacy graphics protocols.

---

*Document updated to reflect Mutineer's modern multiuser telnet architecture.*
*Comparison based on FUNCTIONAL_BBS.md and FUNCTIONAL_MUTINEER.md*
