# Buccaneer Specification

Version: 0.3-draft  
Status: Proposed  
Audience: BBS core developers, language/tooling developers, door authors  
Target host: Multiuser BBS written in C

## 1. Purpose

Buccaneer is a structured, BASIC-like language and execution platform for native BBS door programs. A Buccaneer door is not an external process and not a shell script. It is a tokenized module loaded by the BBS and executed by an embedded VM inside the BBS process.

Buccaneer exists so a BBS can offer:

- fast startup
- per-session isolation
- host-mediated terminal, user, file, message, and storage access
- deterministic quotas and cancellation
- linting, formatting, simulation, profiling, and debugging
- multi-program door applications without raw OS process control

Buccaneer consists of these parts:

1. Source language
2. Formatter
3. Linter
4. Parser and semantic analyzer
5. Tokenizer / bytecode emitter
6. Module verifier
7. Embedded interpreter / VM
8. Host bridge for the C BBS
9. Simulator, debugger, and profiler

## 2. Goals

### 2.1 Primary goals

- Be easy for BASIC-minded door authors to learn
- Stay structured enough to scale beyond toy scripts
- Run safely inside a multiuser BBS written in C
- Expose a stable capability-based host API
- Support multi-program door applications
- Allow safe persistence without exposing raw BBS database writes
- Provide first-class tooling

### 2.2 Non-goals

Buccaneer v1 is not:

- a systems language
- a JIT
- a native-code compiler
- unrestricted filesystem scripting
- unrestricted network scripting
- a raw SQL language
- a pointer or memory hacking language
- classic line-number spaghetti BASIC

## 3. Product model

### 3.1 Workflow

1. Author writes one or more `.bucc` source files.
2. Author runs formatter and linter.
3. Tokenizer emits `.bc` modules and optional `.bmap`.
4. Author runs simulator scenarios, debugger, and profiler locally.
5. Sysop packages the application with `door.json`.
6. BBS installs the package, validates policy, and registers programs.
7. BBS loads modules into an immutable cache.
8. Each caller session gets its own VM instance.

### 3.2 Artifacts

- `*.bucc` - source
- `*.bc` - tokenized module
- `*.bmap` - debug map
- `door.json` - install/runtime manifest
- `*.bsim.json` - simulator scenario
- `buccaneer.toml` - optional project-level tool config

## 4. Language design principles

1. **BASIC-like, not BASIC-broken.** Familiar keywords, explicit blocks, structured flow.
2. **Compile before deploy.** Production BBS nodes execute validated `.bc` modules, not raw source.
3. **Immutable code, isolated runtime state.** Shared loaded module, per-session stacks/heaps/locals.
4. **Capabilities over shortcuts.** The host exposes named powers; door code gets no backdoor escape.
5. **Door data ownership.** A Buccaneer application may read selected BBS data but may only mutate its own storage.
6. **Text UI first.** ANSI, ASCII, and BBS menu flows are central.
7. **Determinism over magic.** Clear quotas, blocking points, yields, and error surfaces.

## 5. Core language model

### 5.1 Source style

- UTF-8 source
- case-insensitive keywords
- case-preserving identifiers
- no line numbers
- one statement per line by default
- `:` statement separator allowed but formatter may rewrite
- comments with `'` or `REM`
- explicit block terminators

### 5.2 Entry points

Each executable module must expose:

- `SUB Main()`

Optional event handlers:

- `SUB OnEnter()`
- `SUB OnInput(line AS STRING)`
- `SUB OnHangup()`
- `SUB OnTimeout()`
- `SUB OnResume()`

### 5.3 Multi-program application model

A Buccaneer application may contain multiple named programs. Within the same installed application, one program may transfer control to another using:

```basic
CHAIN "mail_reader"
CHAIN "scoreboard", args
```

`CHAIN` never performs an OS exec. It requests a host-managed transfer to another Buccaneer program declared in the same installed application, unless cross-application chaining is explicitly allowed by policy.

### 5.4 APP state across chains

`APP` is application-scoped transient state. It survives `CHAIN` within the same application during the same caller session, but is not shared with other applications.

Examples:

```basic
APP.SET("mail.area", "general")
APP.SET("score.mode", "daily")
mode = APP.GET("score.mode", "all")
```

## 6. Type system

### 6.1 Required scalar types

- `INTEGER` - signed 64-bit
- `DOUBLE` - IEEE-754 64-bit
- `BOOLEAN`
- `STRING`
- `DATE`
- `DATETIME`
- `NULL`

### 6.2 Aggregate and compound types

- `ARRAY OF T`
- `MAP OF STRING TO T`

### 6.3 Recommended builtins

Core numeric and string functions include:

- `LEN`
- `STR$`
- `VAL`
- `INT`
- `ROUND`
- `ABS`
- `LEFT$`
- `RIGHT$`
- `MID$`
- `INSTR`
- `LTRIM$`
- `RTRIM$`
- `TRIM$`
- `LOWER$`
- `UPPER$`
- `SPACE$`
- `STRING$`
- `CHR$`
- `ASC`
- `SPLIT`
- `JOIN`
- `FORMAT$`
- `ARRAYLEN`
- `PUSH`
- `POP`
- `KEYS`
- `HASKEY`
- `NEWMAP`
- `NOW`
- `TODAY`

## 7. Control flow additions

### 7.1 Structured conditionals

- `IF / ELSEIF / ELSE / END IF`
- `SELECT CASE / CASE / CASE ELSE / END SELECT`

### 7.2 Loops

- `FOR / NEXT`
- `WHILE / WEND`
- `DO / LOOP`
- `DO / LOOP UNTIL`
- `DO / LOOP WHILE`

### 7.3 Dispatch-friendly BASIC features

Buccaneer deliberately avoids line-numbered `ON ... GOTO`, but it supports structured menu dispatch:

```basic
ON choice CALL HandleMessages, HandleFiles, HandleQuit
```

The selector expression must evaluate to an integer index. Targets must be known procedures with compatible signatures.

### 7.4 Error handling

Buccaneer standardizes on `TRY/CATCH`:

```basic
TRY
    CALL LoadScores()
CATCH err
    TERM.PRINTLN("Error: " + err["message"])
END TRY
```

Classic `ON ERROR` / `ONERR GOTO` is not part of v1.

### 7.5 Exit semantics

- `HALT` is shorthand for `DOOR.EXIT(0)`
- `DOOR.EXIT(code)` terminates the active program cleanly
- `RETURN` exits the current procedure
- `CHAIN` transfers to another declared program

## 8. Terminal and UX model

### 8.1 TERM namespace

The host must provide a `TERM` namespace suitable for normal BBS door work.

Required primitives include:

- `TERM.PRINT`
- `TERM.PRINTLN`
- `TERM.CLS`
- `TERM.COLOR`
- `TERM.PRINTAT`
- `TERM.GETKEY`
- `TERM.INPUT`
- `TERM.INPUT_PASSWORD`
- `TERM.PAUSE`
- `TERM.WIDTH`
- `TERM.HEIGHT`
- `TERM.SUPPORTS_ANSI`
- `TERM.BOX`
- `TERM.SAVE_ATTR`
- `TERM.RESTORE_ATTR`
- `TERM.PRINT_PAGED`
- `TERM.PAGER_BEGIN`
- `TERM.PAGER_LINE`
- `TERM.PAGER_END`

## 9. Host data access model

### 9.1 Read-only BBS data

A Buccaneer door may read certain BBS data when granted capability, including:

- current user identity, alias, security level, flags, time left
- limited user directory lookups through `USERS`
- messagebase data if read capability exists
- filebase metadata if read capability exists
- node and session information

### 9.2 Forbidden core writes

Buccaneer programs may **not** directly write:

- user database
- message database
- filebase catalog
- BBS configuration database
- caller logs
- security/accounting records

If the BBS wants to permit narrowly-scoped state updates like per-door user flags, those must be exposed as explicit policy-gated host calls, not raw database access.

### 9.3 Door-owned data

A Buccaneer application may mutate only its own managed storage:

- `KV` - door-local key/value storage
- `DATA` - door-local structured datasets
- `APP` - session-scoped app state
- `SHARED` - app-scoped transient cross-session state
- `data:/` and `temp:/` file roots via host mediation

## 10. Dataset model

### 10.1 Logical declaration

A Buccaneer application may declare logical datasets in metadata or manifest, for example:

- `scores`
- `matches`
- `inventory`

Door code can reference only logical names declared for that application.

### 10.2 Physical storage identity

Door authors never choose the backing database filename or physical table name. The host generates physical identities during install from:

- application id
- dataset logical name
- install instance id
- version / migration state

This prevents one door from guessing or hijacking another door's storage.

### 10.3 Dataset features

Required `DATA` surface:

- `DATA.INSERT`
- `DATA.UPDATE`
- `DATA.DELETE`
- `DATA.GET`
- `DATA.FIND`
- `DATA.FIND_PAGED`
- `DATA.COUNT`
- `DATA.SUM`
- `DATA.MIN`
- `DATA.MAX`
- `DATA.BEGIN`
- `DATA.COMMIT`
- `DATA.ROLLBACK`

All dataset access is host-mediated and capability-gated. Row version fields are recommended to support optimistic concurrency.

## 11. Shared state and inter-node features

### 11.1 SHARED namespace

`SHARED` is application-scoped transient storage visible across concurrent sessions of the same installed application.

Required operations:

- `SHARED.GET`
- `SHARED.SET`
- `SHARED.DELETE`
- `SHARED.CAS` (compare-and-set)
- `SHARED.LIST_KEYS`

The host decides locking and storage strategy. Semantics must be atomic for single-key operations.

### 11.2 Inter-node messaging

The BBS may expose controlled node messaging:

- `BBS.SEND_NODE_MSG(node, message)`
- optional presence helpers like `BBS.ONLINE_NODES()`

These are capability-gated and auditable.

## 12. Filesystem access model

Buccaneer does not get arbitrary filesystem access.

### 12.1 Virtual roots

Allowed virtual roots:

- `assets:/` - read-only packaged assets
- `data:/` - application-owned persistent files
- `temp:/` - application-owned temporary files

### 12.2 Text file API

High-value v1 file operations include:

- `TEXT.READ_ALL(path)`
- `TEXT.READ_LINES(path)`
- `TEXT.WRITE_ALL(path, text)`
- `TEXT.APPEND_LINE(path, text)`
- `TEXT.EXISTS(path)`

The host must reject path traversal and cross-root access.

## 13. Session, time, and system functions

Useful clock and node functions include:

- `SYS.NOW() AS DATETIME`
- `SYS.TODAY() AS DATE`
- `SESSION.NODE() AS INTEGER`
- `SESSION.ELAPSED_MS() AS INTEGER`
- `USER.TIME_LEFT() AS INTEGER`

## 14. Security model

### 14.1 Capability model

Capabilities are declared by the module and enforced by the host. Examples:

- `term.io`
- `user.read`
- `users.read`
- `msg.read`
- `msg.write`
- `file.read`
- `file.download`
- `kv.read`
- `kv.write`
- `data.read`
- `data.write`
- `shared.read`
- `shared.write`
- `app.state`
- `door.chain`
- `bbs.node_msg`

### 14.2 Throttling

Sensitive calls such as `USERS` enumeration must be runtime-throttled and policy-limited. The spec requires host enforcement, not just linter warnings.

### 14.3 Isolation

- per-session VM isolation
- immutable shared module cache
- no direct raw pointers or host structs
- no arbitrary OS escape
- host-mediated storage only

## 15. Tooling

### 15.1 Linter

Must check syntax, semantics, capability misuse, unsafe loops, dataset misuse, and BBS UX issues.

### 15.2 Formatter

Must be idempotent and preserve meaning.

### 15.3 Simulator

Must support:

- single-session scenarios
- multi-session scenarios for shared-state and inter-node testing
- deterministic time
- transcript capture
- host call trace
- breakpoint and stepping support
- tick/procedure profiler

## 16. Acceptance criteria

A Buccaneer v1 implementation is acceptable only if:

- parser, AST, and semantic analysis accept the defined structured grammar
- tokenizer emits deterministic `.bc`
- module verifier rejects malformed bytecode
- VM safely executes multiple concurrent sessions
- host bridge enforces capability and data ownership rules
- door code cannot write core BBS databases
- datasets use host-generated backing identities
- chaining works inside one application
- sensitive user lookups are runtime-throttled
- simulator can reproduce single-session and multi-session tests deterministically

## 17. Final definition

Buccaneer is:

> A structured, BASIC-like, tokenized scripting language and embedded virtual machine for native BBS door applications, with a capability-gated C host bridge, application-scoped persistence, multi-program chaining, linting, formatting, simulation, debugging, and deterministic multiuser execution inside a bulletin board system.
