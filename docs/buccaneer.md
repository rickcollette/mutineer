<!-- generated-by: gsd-doc-writer -->

# Buccaneer Scripting Language

Buccaneer is Mutineer's embedded scripting language for writing interactive BBS doors in a Pascal-like syntax. The compiler, bytecode VM, and host bridge live in `src/buccaneer/`.

## Overview

```
.bucc source → bucc compiler → bytecode module → VM execution
                                                    ↕
                                              host bridge (terminal I/O, doors)
```

Design specification: `SPECS/BUCCANEER/Buccaneer_SPEC.md`.

## Language Features

- Pascal-like syntax (procedures, functions, variables, control flow)
- Strong typing with inference in semantic pass
- String, integer, boolean, array types
- Module system with `IMPORT`/`EXPORT`
- Door-specific intrinsics via host bridge

### Example: Hello World

From `src/buccaneer/examples/hello.bucc`:

```pascal
PROGRAM Hello;

PROCEDURE Main;
BEGIN
  WriteLn('Ahoy from Buccaneer!');
END;

BEGIN
  Main;
END.
```

### Example: Guess Game

`examples/guess.bucc` — interactive number guessing with user input loop.

## Toolchain

Build from `src/buccaneer/Makefile`:

| Binary | Source | Purpose |
|--------|--------|---------|
| `bucc` | `tools/bucc.c` | Compile .bucc to bytecode |
| `bucc-linter` | `tools/linter.c` | Static analysis |
| `bucc-formatter` | `tools/formatter.c` | Source formatting |
| `bucc-simulator` | `tools/simulator.c` | Standalone VM runner |

### Compile and Run

```bash
cd src/buccaneer
make
./bucc examples/hello.bucc -o hello.bcm
./bucc-simulator hello.bcm
```

Docker-based tests: `src/buccaneer/tests/run-tests-docker.sh`.

## Compiler Pipeline

| Stage | File | Output |
|-------|------|--------|
| Lexer | `lexer.c` | Token stream |
| Parser | `parser.c` | AST (`ast.c`) |
| Semantic | `semantic.c` | Typed AST, symbol table |
| Emit | `emit.c` | Bytecode module (`module.c`) |

Headers in `src/buccaneer/include/bucc_*.h`.

## Virtual Machine

`vm.c` executes bytecode with stack-based operations:

- Arithmetic, comparison, logic
- String operations
- Control flow (jump, call, return)
- Host function calls (`OP_CALL_HOST`)
- Dispatch/coroutine support (`OP_DISPATCH_CALL`)
- Array allocation and indexing

VM states: running, halted, error, chain (door transfer).

## Host Bridge

`host.c` and `bbs.c` connect VM to BBS services:

| Host Function | Purpose |
|---------------|---------|
| Terminal I/O | ReadLine, Write, WriteLn, Cls |
| Door control | DOOR.EXIT, DOOR.CHAIN |
| User info | Handle, level, credits, time |
| File I/O | Limited file access for scripts |

Host API header: `include/bucc_host.h`.

Door manifest example: `examples/door.json` for Buccaneer door metadata.

## Package Format

`package.c` handles compiled module packaging:

- Module header with version and exports
- Procedure table with bytecode offsets
- String/constant pools
- Debug symbols (optional)

Header: `include/bucc_package.h`.

## Integration Status

Buccaneer is **implemented but not production-ready** for in-BBS door launching. Known issues from `BUCC_TODO.md`:

### Critical Bugs

| ID | Issue |
|----|-------|
| BUG-1 | `DOOR.CHAIN` sets `VM_HALT` instead of `VM_CHAIN` |
| BUG-2 | `DOOR.EXIT` discards exit code |
| BUG-3 | `OP_ARRAY_NEW` with count=0 calls `malloc(0)` |
| BUG-4 | `OP_DISPATCH_CALL` discards const on module pointer |
| BUG-5 | Out-of-range dispatch leaves stack inconsistent |
| BUG-6 | `OP_CALL_HOST` silently truncates args beyond 16 |
| BUG-7 | `OP_DISPATCH_CALL` emits base_proc_id=0 |

### Missing Features

- Full integration with `doors.c` door launcher
- Complete DOOR.SYS dropfile generation from VM
- Production error reporting and timeout handling
- Menu registration for `.bucc` doors in DB

### Recommended Use

1. **Development/testing:** Use `bucc-simulator` with compiled modules
2. **Production doors:** Use native or DOSBox runners
3. **Track progress:** Monitor `BUCC_TODO.md` and `SPECS/BUCCANEER/`

## Testing

```bash
cd src/buccaneer/tests
make
./run-tests-docker.sh
```

Test suites: lexer, parser, value, VM, package, e2e.

## Comparison with Plugins

| Aspect | Buccaneer | Plugin (.so) |
|--------|-----------|--------------|
| Language | Pascal-like script | C |
| Build | bucc compiler | gcc/clang shared lib |
| Reload | Recompile script | Reload .so |
| Sandboxing | VM bytecode | None (native code) |
| Performance | Interpreted | Native |
| ABI stability | Internal | `bbs_plugin_api.h` v1.0 |

See [Doors and Scripting](doors-and-scripting.md) for selection guidance.

## Related Documentation

- [Doors and Scripting](doors-and-scripting.md)
- `SPECS/BUCCANEER/Buccaneer_SPEC.md`
- `BUCC_TODO.md`
