# Buccaneer Follow-Up

This page tracks live Buccaneer follow-up candidates that are suitable for
GitHub issues. Completed audit notes do not live here; they are covered by
tests and the current guides.

## Current Follow-Up Candidates

- `DATA.QUERY` Add a constrained query helper for door packages that need read-only access to small, declared datasets without exposing arbitrary SQL.
- `TERM.SELECT` Add a structured menu/select helper for doors that currently hand-roll numbered prompts.
- `PKG.SIGNATURE` Add optional package signature verification for installed Buccaneer door bundles.
- `BUCC.FORMAT` Add a formatter mode to the `bucc` tool once the parser and AST trivia model can preserve enough source shape.

## Completed Foundations

The current build and test suite already cover:

- Top-level CMake integration for `bucc` and the BBS embedding.
- Host dispatch for terminal, session, user, data, text, KV, shared, message,
  file, and door control calls documented in [Host API](host-api.md).
- `USER.FLAGS()`, `BBS.ONLINE()`, `DOOR.EXIT()`, and `DOOR.CHAIN()` behavior.
- Deep `SHARED.CAS()` equality for scalar and structured values.
- Capability checks for package-host access.

Use `scripts/create-bucc-github-issues.sh --dry-run` to preview issues from the
current candidate list.
