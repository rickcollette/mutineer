# Buccaneer Specification Bundle

Version: 0.3-draft  
Date: 2026-02-27

This bundle contains the current Buccaneer language and runtime design for a C-based multiuser BBS that embeds Buccaneer door programs as native VM modules.

## Included documents

- `Buccaneer_SPEC.md` - core language/platform spec
- `BUCCANEER_GRAMMAR_SPEC.md` - lexical, syntactic, and grammar rules
- `BUCCANEER_BYTECODE_VM_SPEC.md` - module format, opcode set, VM execution model
- `BUCCANEER_AST_SPEC.md` - abstract syntax tree model and invariants
- `BUCCANEER_SEMANTIC_ANALYSIS_SPEC.md` - binding, typing, control-flow, capability, and policy analysis
- `BUCCANEER_C_EMBEDDING_API_SPEC.md` - C host bridge and embedding API
- `DOOR_JSON_SCHEMA_SPEC.md` - `door.json` manifest contract
- `BUCCANEER_PACKAGING_INSTALL_SPEC.md` - packaging, install, activation, upgrade, rollback
- `FILESYSTEM.md` - repository, install, runtime, and deployed door hierarchy

## Design center

Buccaneer is not an external DOS-era door launcher. It is a structured BASIC-like language, tokenized ahead of time, executed inside the BBS process by a stack VM, and constrained by a host capability model.

Three hard rules shape the design:

1. Door code may **read** selected BBS state but may not write core BBS databases.
2. Door code may create and mutate only **door-owned datasets** and door-owned key/value or file storage.
3. Multi-program Buccaneer applications must be possible without turning the language into unstructured spaghetti BASIC.

## Current platform decisions

- Source extension: `.bucc`
- Tokenized module extension: `.bc`
- Optional debug map: `.bmap`
- Manifest: `door.json`
- Structured error handling: `TRY/CATCH`
- `HALT` is shorthand for `DOOR.EXIT(0)`
- Chaining between Buccaneer programs is supported through `CHAIN`
- Door-local datasets are declared logically; the host generates physical storage identities
- Sensitive user enumeration is runtime-throttled and policy-gated
- Shared transient state is app-scoped and host-mediated

## Recommended implementation order

1. Lexer, parser, AST
2. Semantic analysis and linter
3. Formatter
4. Bytecode emitter and verifier
5. VM and host bridge
6. Simulator with single-session scenarios
7. Shared-state, datasets, profiler, and debugger
8. Packaging, signing, and deployment tooling
