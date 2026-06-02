# Cursor Prompt - Implement DOSBox-backed DOS doors in Mutineer end-to-end

You are Cursor operating as a principal systems engineer. Your job is to implement complete, production-grade DOS door support in this Mutineer codebase. Do not stop at recommendations. Do not leave TODOs. Do not ask questions. Read the relevant code paths end-to-end, then implement the feature fully.

Your target is a new class of door: a DOS program that runs under official DOSBox and communicates with the caller through the live Mutineer telnet socket using DOSBox socket inheritance.

## Non-negotiable requirements

1. Preserve existing native door behavior unless explicitly upgraded.
2. Add a dedicated DOSBox door runner. Do not bolt DOSBox awkwardly into the old `system()` path.
3. The DOSBox runner must be session-safe and multiuser-safe.
4. The DOSBox runner must use a per-launch runtime directory, not a shared mutable door directory.
5. The DOSBox runner must pass the caller socket to DOSBox using `-socket <fd>` and DOSBox serial nullmodem socket inheritance.
6. The DOSBox config must set `serial1=nullmodem inhsocket:1 telnet:1` by default.
7. The DOS door must run from an isolated runtime copy of the master door files.
8. The selected primary dropfile must be honored.
9. Node status/activity must update while the door runs and restore afterward.
10. No stubs, placeholders, or "good enough for now" shortcuts.

## What you must read first

### Sample DOS DOOR Aladdin

- `SPECS/DOSDOORS/aladdin/` - this is a dos door we can test with

Read and trace all relevant code for:

- `src/doors.c`
- `src/session.c`
- `include/bbs_doors.h`
- config loading/defaults for `doors_path` and `dropfile_path`
- DB accessors for doors
- node state upsert/clear behavior
- any existing helper utilities for paths, file copying, directory creation, logging, process handling, or config parsing

Do not make assumptions about how Mutineer currently works. Verify it in code first.

## What you must implement

### 1. Split the door launcher into runners

Refactor the current door launcher into:

- `door_launch()` as a dispatcher
- `door_launch_native()`
- `door_launch_dosbox()`

Keep native behavior working.

### 2. Add door metadata needed for DOSBox doors

Implement a robust way to identify a DOSBox door. Preferred implementation:

- extend the doors table / DB layer with:
  - `runner`
  - `manifest`
  - `enabled`
  - `timeout_sec`

If full schema migration support is not already present, add it properly. Do not hardcode fragile assumptions.

If you need a compatibility fallback for legacy rows, make legacy rows default to `runner=native`.

### 3. Add a door manifest parser

Implement a manifest parser for DOSBox doors. It must support at least:

- `runner`
- `name`
- `master_dir`
- `startup`
- `dropfile`
- `dropfile_dest`
- `machine`
- `memsize`
- `core`
- `cycles`
- `serial_telnet`
- `usedtr`
- `timeout_sec`
- `copy_mode`
- `cleanup_on_exit`

Validate every field. Reject unsafe paths. Reject missing required fields. Reject startup targets that escape the runtime tree.

### 4. Add config fields for DOSBox execution

Add new config keys with sane defaults:

- `dosbox_path`
- `door_runtime_path`
- `door_copy_mode`
- `door_default_timeout_sec`
- `door_cleanup_on_exit`
- `door_keep_failed_runs`

Update config defaults and parsing cleanly.

### 5. Implement per-launch runtime preparation

For each DOSBox door launch, create a unique runtime tree like:

- `<door_runtime_path>/<doorname>/node<NN>/<launch-id>/game`
- `<door_runtime_path>/<doorname>/node<NN>/<launch-id>/logs`

Do not execute from the master door directory.

Populate `game/` from the configured master door directory using a correct recursive file copy. If there is already a utility for recursive copy, use it. Otherwise implement one cleanly.

### 6. Implement disciplined dropfile generation

Write the configured primary dropfile into the correct location inside the runtime tree.

You may still generate the other compatibility dropfiles if that helps compatibility, but the primary dropfile must be treated as authoritative and must be written to the correct target directory.

Do not leave the current "write everything into one shared per-door folder" behavior in place for DOSBox doors.

### 7. Generate a per-run DOSBox config

Generate a DOSBox config file inside the runtime root. It must include:

- safe baseline DOSBox settings
- serial config using nullmodem socket inheritance
- autoexec that mounts only the runtime `game/` directory
- startup command that launches the configured DOS door entrypoint
- `exit` at the end

Default serial config must be:

`serial1=nullmodem inhsocket:1 telnet:1`

Support optional `usedtr:1` if the manifest requests it.

### 8. Launch DOSBox correctly

Do not use `system()` for DOSBox doors.

Use `fork()` and `execv()`/`execve()` with a proper argv array.

Pass at least:

- `-conf <generated_conf>`
- `-socket <session_fd>`
- `-exit`

Use the configured DOSBox binary path.

### 9. Supervise the child process

In the parent:

- update node activity to `door:<doorname>`
- supervise the DOSBox child
- enforce timeout
- handle normal exit, non-zero exit, signal exit, and hang
- restore node activity to `menu` afterward
- perform cleanup according to config

Do not blindly block forever in `waitpid()` without timeout logic.

### 10. Handle disconnect and shutdown paths

If the caller disconnects or the system is shutting down while DOSBox is active, terminate the child cleanly, then force-kill if needed.

Do not leave orphan DOSBox processes.

### 11. Preserve session continuity

When the DOS door exits, return control to Mutineer cleanly.

Make sure the user is still in a valid session state, gets a clear return/failure message where appropriate, and is returned to the menu flow.

### 12. Logging

Add clear logging for:

- launch start
- manifest/config path
- runtime path
- pid
- timeout
- exit status
- cleanup result
- failures

### 13. Tests

Add or update tests so the DOSBox runner is covered. If DOSBox is not available in test environments, use a controlled fake executable that simulates:

- success
- failure
- hang

Test at least:

- manifest parsing/validation
- runtime directory creation
- DOSBox argv generation
- timeout behavior
- node activity transitions
- native doors still work

## Implementation constraints

- Do not widen filesystem exposure by mounting broad host directories into DOSBox.
- Do not mount the entire BBS root into DOSBox.
- Do not trust manifest path input.
- Do not trust startup path input.
- Do not shell-escape untrusted strings into a single command string if argv-based exec will do.
- Do not regress native doors.
- Do not leave old and new logic duplicated in confusing ways. Consolidate cleanly.

## Expected end state

When you are done, the codebase must support all of the following:

1. A legacy native door still launches normally.
2. A DOSBox door can be configured and launched from the existing door menu flow.
3. Multiple nodes can launch the same DOSBox door without file collisions.
4. DOSBox receives the live caller socket through `-socket`.
5. The DOS door communicates through DOSBox serial nullmodem socket inheritance.
6. Node activity reflects the active door.
7. Timeouts and disconnects are handled safely.
8. Logs and tests cover the new behavior.
9. Test by using SPECS/DOSDOORS/aladdin as the test DOS Door Game

## Deliverables you must complete in code

- updated headers and structs
- updated DB model and migrations as required
- updated config defaults/parsing
- DOSBox manifest support
- DOSBox runtime prep/copy logic
- per-run DOSBox config generation
- DOSBox launcher/supervisor
- updated tests
- any needed admin/seed adjustments so DOSBox doors are representable in the system

Do the work end-to-end. Do not stop at analysis.
