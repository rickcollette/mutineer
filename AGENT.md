You are a senior engineer brought in to make this repository COMPLETE and production-ready. Do an end-to-end audit of the entire codebase and implement everything that is stubbed, partial, TODO/FIXME, “not implemented”, placeholder logic, or missing critical glue. Do not stop at analysis—ship working code.

NON-NEGOTIABLES
- No stubs, no placeholders, no “left as an exercise”, no “for now”.
- If a feature is referenced by docs/config/menus/routes/CLI flags but not implemented, implement it.
- If a function exists but is never called or the workflow is broken/incomplete, fix the workflow end-to-end.
- Make changes in small, reviewable commits/patches (logical chunks). Avoid large refactors unless necessary.

PROCESS (FOLLOW IN ORDER)
1) Establish truth:
   - Identify language/tooling: build system (Make/CMake, Go modules, Cargo, Node, etc), entrypoints, services, binaries, scripts.
   - Run the existing build/test commands. If none exist, create a minimal, sane default (build + unit test runner).
   - Read CI config if present and ensure local commands match CI.

2) Find incompleteness systematically (do not guess):
   - Search the repo for: TODO, FIXME, HACK, XXX, STUB, “not implemented”, “WIP”, “placeholder”, “dummy”, “temp”, “return 0”, “return nil”, “panic”, “abort”, “unreachable”, “assert(0)”, “exit(1)”, “log.Fatal”.
   - Also find incomplete workflows: routes/handlers that return empty responses, unvalidated inputs, config options not used, menu items that lead nowhere, API endpoints without real backing logic, DB schema without migrations, background jobs not wired, etc.
   - Produce a GAP LIST: each gap has (file:line, what’s missing, why it matters, what depends on it).

3) Define “complete” for THIS repo:
   - Trace the main workflows end-to-end (startup → config → DB → core services → IO/transport → shutdown).
   - For each workflow define acceptance criteria in plain language (what must work, how to verify).

4) Implement fixes (highest impact first):
   - Replace partial logic with real implementations using existing patterns/libs already in repo.
   - Add missing modules/components only when required.
   - Ensure robust error handling (no silent failures), correct resource cleanup, timeouts, and sane defaults.

5) Make it verifiable:
   - Add/expand unit tests + integration tests for every fixed gap.
   - Add test fixtures where needed.
   - Add a single “smoke test” command that a human can run to validate the whole system quickly.

6) Production hygiene pass:
   - Logging: consistent levels, structured where appropriate, no leaking secrets.
   - Config: validate on startup, clear error messages, documented options.
   - Security: input validation, authz boundaries, safe file handling, safe extraction/parsing, avoid injection patterns.
   - Reliability: retries where appropriate, backoff, idempotency on jobs, safe concurrency.
   - Documentation: update/complete README and docs to match reality (no dead docs).

OUTPUT REQUIREMENTS
- Start by printing a concise GAP LIST (bullet list) with file paths and what you will fix.
- Then implement fixes iteratively. After each patch chunk:
  - state what changed
  - show the commands to run to verify
  - confirm tests added/updated
- End with:
  - “How to run” steps
  - full test command list
  - a final checklist of what was completed

CONSTRAINTS
- Do not introduce new major dependencies unless necessary; prefer existing libs.
- Do not change public APIs unless needed; if you must, update all callers and docs.
- Keep changes coherent and minimal, but COMPLETE.

Now begin: scan the repo, generate the GAP LIST, and start fixing.