# Buccaneer

**Buccaneer** is Mutineer's embedded language for native BBS doors — structured BASIC-like syntax, compile-to-bytecode, run inside the BBS process with capability-gated host APIs.

```
.bucc source  →  bucc compiler  →  .bc module  →  VM + host bridge  →  caller session
```

## Documentation

| Guide | Audience | Description |
|-------|----------|-------------|
| [Programmer's Guide](programmers-guide.md) | Door authors | Language syntax, types, control flow, examples |
| [Host API Reference](host-api.md) | Door authors | `TERM`, `USER`, `DATA`, `KV`, `DOOR`, … namespaces |
| [Toolchain](toolchain.md) | Authors & devs | `bucc`, linter, formatter, simulator |
| [Door Packages](door-packages.md) | Sysops & authors | `door.json`, capabilities, install layout |

## Mutineer integration

| Topic | Document |
|-------|----------|
| Doors overview (native, DOS, BUCC) | [Doors and Scripting](../doors-and-scripting.md) |
| Architecture / VM placement | [Architecture](../architecture.md) |

## Implementation status

Buccaneer is under active development. The compiler and VM in `src/buccaneer/` are usable standalone; full BBS launch via `runner=bucc` and some host functions are still being wired (track [GitHub issues](https://github.com/rickcollette/mutineer/issues?q=label%3Abuccaneer)).

Always test doors with `bucc-simulator` before deploying to a live BBS.

## Formal specifications

Low-level specs for compiler and VM implementers live under `SPECS/BUCCANEER/`:

| Spec | Topic |
|------|-------|
| `Buccaneer_SPEC.md` | Language and platform overview |
| `BUCCANEER_GRAMMAR_SPEC.md` | Lexer and parser grammar |
| `BUCCANEER_SEMANTIC_ANALYSIS_SPEC.md` | Type checking and symbols |
| `BUCCANEER_BYTECODE_VM_SPEC.md` | Opcodes and VM behavior |
| `BUCCANEER_C_EMBEDDING_API_SPEC.md` | C embedding API |
| `DOOR_JSON_SCHEMA_SPEC.md` | Install manifest |

The programmer's guide summarizes what door authors need; specs are authoritative for tooling developers.
