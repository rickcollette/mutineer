# FILESYSTEM.md

Version: 0.3-draft  
Status: Proposed

This document defines the repository, install, runtime, and deployed door hierarchy for Buccaneer.

## 1. Source repository layout

```text
buccaneer/
  README.md
  Buccaneer_SPEC.md
  BUCCANEER_GRAMMAR_SPEC.md
  BUCCANEER_BYTECODE_VM_SPEC.md
  BUCCANEER_AST_SPEC.md
  BUCCANEER_SEMANTIC_ANALYSIS_SPEC.md
  BUCCANEER_C_EMBEDDING_API_SPEC.md
  DOOR_JSON_SCHEMA_SPEC.md
  BUCCANEER_PACKAGING_INSTALL_SPEC.md
  FILESYSTEM.md
  src/
  include/
  runtime/
  tests/
  examples/
```

## 2. Installed runtime layout

```text
/bbs/
  bin/
    bbs
    bucc
  lib/
    libbuccvm.so
  etc/
    bbs.conf
    buccaneer/
  doors/
    bucc/
      trivia/
        door.json
        main.bc
        play_round.bc
        scores.bc
        assets/
  var/
    cache/buccaneer/
    log/buccaneer/
    spool/buccaneer/
```

## 3. Security rules

- no direct write access to installed module or asset files by door code
- no cross-application file access
- no raw physical dataset filenames in user code
- no parent traversal in any manifest path
