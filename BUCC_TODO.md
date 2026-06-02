# BUCC_TODO — Buccaneer VM Audit

Findings from a direct review of every `.c` and `.h` file in `src/buccaneer/`.
Issues are grouped by severity and type. Line numbers are exact.

---

## BUGS — Active Code Defects

### BUG-1 `host.c:745` — `DOOR.CHAIN` sets `VM_HALT` instead of `VM_CHAIN`
`bucc_host_door_chain()` terminates the VM with `ctx->vm->status = VM_HALT`.
The runner in `bbs.c:228-235` checks for `VM_CHAIN` to populate `chain_target`
and `chain_args`. Setting `VM_HALT` causes the runner to treat a chain as a
normal successful exit and silently discard the chain target.
```c
// host.c:745 — wrong:
ctx->vm->status = VM_HALT;
// should be:
ctx->vm->status = VM_CHAIN;
```

### BUG-2 `host.c:721` — `DOOR.EXIT` discards the exit code
`bucc_host_door_exit()` reads the argument (`code = (int)bucc_value_to_int(...)`)
then immediately suppresses it with `(void)code;`. The `exit_code` field of
`bucc_door_result_t` is never populated; callers cannot distinguish a normal
exit from one with a specific code.

### BUG-3 `vm.c:807` — `OP_ARRAY_NEW` with `count=0` calls `malloc(0)`
```c
bucc_value_t* items = malloc(count * sizeof(bucc_value_t));
for (int i = count - 1; i >= 0; i--) {
    items[i] = bucc_vm_pop(vm);   // UB if items == NULL
}
```
`malloc(0)` is implementation-defined; on platforms that return NULL, the
subsequent indexed write is undefined behavior. Guard with `if (count > 0)`.

### BUG-4 `vm.c:1076` — `OP_DISPATCH_CALL` discards `const` on `vm->module`
```c
bucc_module_proc_t* proc = &((bucc_module_t*)vm->module)->procedures[proc_id];
```
`vm->module` is `const bucc_module_t*`. The cast to `bucc_module_t*` discards
`const`, which is undefined behavior if the original was truly `const`. Use a
local `const bucc_module_proc_t*` instead.

### BUG-5 `vm.c:1060-1090` — `OP_DISPATCH_CALL` out-of-range selector leaves stack inconsistent
When `index < 1` or `index > target_count` the selector is consumed but nothing
is pushed. Any caller that expects a return value will trigger a silent stack
underflow on the next operation.

### BUG-6 `vm.c:709` — `OP_CALL_HOST` silently truncates args beyond 16
```c
int actual_argc = argc < 16 ? argc : 16;
```
No error is raised. Scripts that pass more than 16 arguments will have trailing
args silently dropped, producing wrong behavior with no diagnostic.

### BUG-7 `emit.c:793` — `OP_DISPATCH_CALL` always emits base_proc_id=0
```c
bucc_emit_op_u16(emit, OP_DISPATCH_CALL, 0);   // 0 is wrong for any real ON CALL
```
The `ON CALL` statement emitter never looks up the actual starting procedure ID.
Every `ON CALL` dispatch will incorrectly start from procedure 0, branching into
the wrong procedures.

### BUG-8 `host.c:614` — `SHARED.CAS` uses shallow equality for map/array
```c
matches = bucc_value_equals(*current, args[1]);  // line 614
```
`bucc_value_equals` (value.c:579) does NOT perform deep comparison for arrays
and maps — it compares pointer identity only. `bucc_value_equal` (value.c:393)
is the correct deep-equality function used everywhere else. CAS on a map or array
expected value will always report mismatch.

---

## MISSING HOST FUNCTIONS — Dispatch Gaps

### MISSING-1 `TERM.GOTOXY` — registered but never dispatched
`host_functions[]` (host.c:31) registers `TERM.GOTOXY` and `bucc_term_api_t`
(host.h:40) has the `.gotoxy` function pointer. However `bucc_host_dispatch()`
(host.c:1131-1140) has no case for `"GOTOXY"` in the TERM block, and no
`bucc_host_term_gotoxy()` function exists anywhere. Calling `TERM.GOTOXY` at
runtime always returns "Unimplemented host function".

Fix needed:
- Add `bucc_host_term_gotoxy()` in `host.c`
- Declare it in `bucc_host.h`
- Add dispatch case in `bucc_host_dispatch()`

### MISSING-2 `TERM.PAUSE`, `TERM.SUPPORTS_ANSI`, `TERM.INPUT_PASSWORD` — struct fields with no implementation
`bucc_term_api_t` (host.h:43-47) defines function pointers `.pause`,
`.supports_ansi`, and `.input_password`. The simulator implements them
(simulator.c:109,113,136). But:
- None are in `host_functions[]` — BUCC scripts cannot call them
- No handler functions (`bucc_host_term_pause` etc.) exist in `host.c`
- No declarations in `bucc_host.h`
- `TERM.INPUT` (host.c:266-287) always uses `.input`, never `.input_password`

### MISSING-3 `SESSION.NODE()` and `SESSION.ELAPSED_MS()` — spec-defined, absent
The Buccaneer spec (Buccaneer_SPEC.md:399-400) documents:
- `SESSION.NODE() AS INTEGER` — returns node number
- `SESSION.ELAPSED_MS() AS INTEGER` — returns ms elapsed this session

Neither is in `host_functions[]`, neither is dispatched, neither has a handler.
`BBS.NODE` partially covers the first but under a different namespace.

### MISSING-4 `USER.TIME_LEFT` name mismatch vs spec
Spec (Buccaneer_SPEC.md:401) documents `USER.TIME_LEFT()` but the implementation
registers it as `USER.TIMELEFT` (host.c:41, dispatch at host.c:1146).
Scripts written to the spec using `USER.TIME_LEFT` will get "Unknown host function".

---

## MISSING BBS.C API WIRING

### WIRE-1 `bbs.c` — `bucc_door_set_users_api()`, `bucc_door_set_msg_api()`, `bucc_door_set_file_api()` don't exist in `bucc_bbs.h` or `bbs.c`
`bucc_bbs.h` (lines 81-86) exposes convenience setters for `term`, `user`,
`data`, `kv`, `text`, `bbs` — but NOT for `users`, `msg`, or `file`. Callers
using `bucc_door_runner_t` cannot bind these APIs without calling into the lower
`bucc_host_set_*` layer directly, which is undocumented in the embedding API.

Missing declarations (needed in `bucc_bbs.h` and implemented in `bbs.c`):
```c
void bucc_door_set_users_api(bucc_door_runner_t* runner, bucc_users_api_t* api, void* ctx);
void bucc_door_set_msg_api  (bucc_door_runner_t* runner, bucc_msg_api_t*   api, void* ctx);
void bucc_door_set_file_api (bucc_door_runner_t* runner, bucc_file_api_t*  api, void* ctx);
```

### WIRE-2 `bbs.c:108-136` — All context pointers passed as NULL
Every `bucc_door_set_*` in `bbs.c` calls `bucc_host_set_*(..., NULL)`. Example:
```c
void bucc_door_set_term_api(bucc_door_runner_t* runner, bucc_term_api_t* api) {
    bucc_host_set_term_api(runner->host_ctx, api, NULL);  // NULL ctx!
}
```
`ctx->session_ctx` (and `user_ctx`, `data_ctx` etc.) will always be NULL.
Every host function that passes this as the `void* ctx` to the API function
pointer will segfault or malfunction when the API implementation dereferences it.
The setters need a `void* context` parameter, or `bucc_door_runner_t` needs to
carry context pointers alongside APIs.

### WIRE-3 `src/doors.c` — No `runner=bucc` dispatch path
`door_launch()` in `src/doors.c:873-892` knows about `runner=dosbox` and falls
back to `runner=native`. There is no `runner=bucc` case. BUCC doors exist as a
complete subsystem but cannot be launched from within the BBS's door menu.
`bucc_door_run_simple()` / `bucc_door_runner_t` are never called from any BBS
source file.

---

## SECURITY ISSUES

### SEC-1 `host.c` — Capability enforcement completely absent from `bucc_host_dispatch()`
Every entry in `host_functions[]` has a `.capability` string (e.g. `"data.write"`).
`bucc_door_runner_t` tracks `allowed_capabilities` as a bitmask. Spec Section 14.1
requires the host to enforce capability checks at call time.

`bucc_host_dispatch()` never checks capabilities. Any script, regardless of what
`door.json` declares, can call any function. `DATA.DELETE`, `TEXT.WRITEALL`,
`BBS.SENDMSG` etc. are available to a script that declares only `term.io`.

Fix: Before dispatching, check `capability_from_string(func->capability) &
((bucc_host_context_t*)ctx)->allowed_capabilities` — but note the host context
doesn't currently carry the capability mask; it must be threaded in.

### SEC-2 `bbs.c:303` — `bucc_door_run_simple()` grants `BUCC_CAP_ALL`
```c
runner->allowed_capabilities = BUCC_CAP_ALL;  // grants everything
```
The "simple" convenience runner is supposed to be for quick invocations with
minimal setup. Granting all capabilities including `data.write`, `text.write`,
and `bbs.message` to an untrusted module path is dangerous. Should default to
`BUCC_CAP_SAFE`.

### SEC-3 `host.c:954-963` — Throttle only covers `USERS.FIND`
Rate limiting is implemented only for `USERS.FIND`. Spec Section 14.2 says
sensitive calls should be "runtime-throttled and policy-limited". At minimum,
`BBS.SENDMSG`, `MSG.POST`, `DATA.INSERT/UPDATE/DELETE` should be throttled.
The `throttled: true` flag on entries in `host_functions[]` (e.g. line 43)
exists but is never read.

### SEC-4 `host.c:557-622` — `SHARED.*` operations not thread-safe
`ctx->shared_state` is a `bucc_map_t*` that can be accessed by multiple concurrent
BUCC door sessions. `SHARED.GET`, `SHARED.SET`, and `SHARED.CAS` read/write it
with no mutex. BUCC's value of `SHARED.CAS` as an atomic primitive is entirely
broken without a lock.

---

## DEAD CODE / NEVER EMITTED

### DEAD-1 `OP_RANGE_TEST` (0x51) — opcode implemented but never emitted
`vm.c:1092-1117` fully implements `OP_RANGE_TEST`. `bucc_opcode_name()` names it.
But there is no AST node for a range expression, no parser rule produces one, and
`emit.c` has no code path that emits `OP_RANGE_TEST`. The opcode is unreachable
unless raw bytecode is hand-crafted.

Either:
- Add `x BETWEEN low AND high` grammar + AST node + emitter path, or
- Remove the opcode and trim the VM switch to avoid confusion.

---

## BUILD / INTEGRATION GAPS

### BUILD-1 Buccaneer has no entry in the main `CMakeLists.txt`
`src/buccaneer/` builds via its own standalone `Makefile`. It is not referenced
in `/home/megalith/mutineer-bbs/CMakeLists.txt`. The BUCC library is not linked
into `mutineer`, its tests are not in `ctest`, and it will not be built by the
normal `cmake --build` workflow.

### BUILD-2 `make test` in `src/buccaneer/` does not run test suites
```makefile
test: $(CLI)
    @echo "Running basic tests..."
    @$(CLI) version
    @echo "Tests passed!"
```
`test_vm.c`, `test_e2e.c`, `test_lexer.c`, `test_parser.c`, `test_package.c`,
`test_value.c` are all written and present in `tests/` but are not compiled by the
Makefile's `test` target. Running `make test` only checks that `bucc version`
exits 0.

Fix: Add test executables to the Makefile and run them under `test:`.

---

## DESIGN INCONSISTENCIES

### DESIGN-1 Two equality APIs with different semantics; wrong one in `SHARED.CAS`
- `bucc_value_equal(a*, b*)` (value.c:393) — deep equality (recurses arrays/maps)
- `bucc_value_equals(a, b)` (value.c:579) — shallow; arrays/maps compare pointer identity

`SHARED.CAS` at host.c:614 uses `bucc_value_equals`, which is incorrect for any
compound value. This should use `bucc_value_equal`. Both functions should be
documented clearly or one removed.

### DESIGN-2 `USER.FLAGS` is missing from the user API
`bucc_user_api_t` (host.h:50-57) and `host_functions[]` (host.c:37-41) expose
`NAME`, `ALIAS`, `ID`, `SECURITY`, `TIMELEFT`. The `bucc_user_api_t` struct has
a `get_flags` pointer (host.h:56) but no corresponding `USER.FLAGS` function is
in `host_functions[]` or dispatched. Scripts cannot read user AR/AC flags.

### DESIGN-3 `bucc_door_result_t.exit_code` is never set
The struct (bucc_bbs.h:38) has an `exit_code` field. Neither `bucc_door_run()`
nor `bucc_host_door_exit()` populates it. It will always be 0 regardless of what
value the script passes to `DOOR.EXIT`.

### DESIGN-4 Simulator wraps all TERM functions but main host layer does not
`tools/simulator.c` implements `gotoxy`, `pause`, `supports_ansi`, and
`input_password`. The main `host.c` does not. Any BUCC door that tests
`TERM.SUPPORTS_ANSI` or uses `TERM.PAUSE` works in the simulator but fails in
production with "Unimplemented host function".

---

## SUMMARY TABLE

| ID | File | Severity | Category |
|----|------|----------|----------|
| BUG-1 | host.c:745 | Critical | Bug — DOOR.CHAIN uses wrong VM status |
| BUG-2 | host.c:721 | Medium | Bug — exit code ignored |
| BUG-3 | vm.c:807 | High | Bug — malloc(0) UB in ARRAY_NEW |
| BUG-4 | vm.c:1076 | Low | Bug — const discard in DISPATCH_CALL |
| BUG-5 | vm.c:1060 | Medium | Bug — DISPATCH_CALL out-of-range leaves stack broken |
| BUG-6 | vm.c:709 | Medium | Bug — CALL_HOST silently truncates args>16 |
| BUG-7 | emit.c:793 | Critical | Bug — DISPATCH_CALL always emits base_proc=0 |
| BUG-8 | host.c:614 | High | Bug — SHARED.CAS uses wrong equality |
| MISSING-1 | host.c:1131 | High | TERM.GOTOXY registered, never dispatched |
| MISSING-2 | host.c | Medium | TERM.PAUSE / SUPPORTS_ANSI / INPUT_PASSWORD absent |
| MISSING-3 | host.c | Medium | SESSION.NODE and SESSION.ELAPSED_MS absent |
| MISSING-4 | host.c:41 | Low | USER.TIME_LEFT name mismatch with spec |
| WIRE-1 | bbs.c | Medium | Missing users/msg/file API setters |
| WIRE-2 | bbs.c:108 | Critical | All API context pointers always NULL |
| WIRE-3 | src/doors.c | Critical | No runner=bucc dispatch path — BUCC doors unlaunchable |
| SEC-1 | host.c:1108 | Critical | Capability enforcement completely absent |
| SEC-2 | bbs.c:303 | High | run_simple() grants BUCC_CAP_ALL |
| SEC-3 | host.c:954 | Medium | Throttle only covers USERS.FIND |
| SEC-4 | host.c:557 | High | SHARED.* not thread-safe |
| DEAD-1 | vm.c:1092 | Low | OP_RANGE_TEST unreachable — never emitted |
| BUILD-1 | CMakeLists.txt | High | BUCC not in main build |
| BUILD-2 | Makefile:test | Medium | Test suites not run by `make test` |
| DESIGN-1 | value.c | Medium | Dual equality API; wrong one in SHARED.CAS |
| DESIGN-2 | host.c | Low | USER.FLAGS has getter in struct but no dispatch |
| DESIGN-3 | bucc_bbs.h:38 | Low | exit_code field never written |
| DESIGN-4 | host.c vs simulator.c | Medium | Simulator implements 4 TERM fns; host layer does not |
