# Mutineer BBS Feature Matrix

Review date: 2026-07-14

Scope: organized status snapshot after completing the remaining non-100% review
items. Detailed historical matrices live under `archived-feature-matrices/`.

Terminology note: Buccaneer is Mutineer's interpreted language for writing
addons, games, doors, and extensions.

| Priority level | Percentage complete | State | Description | Dependancy |
|---|---:|---|---|---|
| P0 | 100% | Complete | PLANK deadletter storage now builds object-id JSON dynamically, safely handles 100 full object IDs, rejects object lists beyond the supported cap, binds bundle/text values with prepared statements, and is covered by FK-enabled regression tests. | `src/plank/plank_store.c`, `tests/plank/test_store.c` |
| P0 | 100% | Complete | PLANK quarantine and related store APIs now use prepared/bound statements for quote-containing node addresses, reasons, peer/link/area/object/import/config/audit values, with runtime tests for quoted text and optional FK fields. | `src/plank/plank_store.c`, `tests/plank/test_store.c` |
| P0 | 100% | Complete | Public generated website docs are aligned with Buccaneer as Mutineer's interpreted language for addons, games, doors, and extensions. | `website/`, `docs/`, `tests/test_docs_consistency.c` |
| P1 | 100% | Complete | `scripts/create-bucc-github-issues.sh` reads current follow-up candidates from `docs/status/BUCC_TODO.md` and refuses archived completed issue IDs. | `scripts/create-bucc-github-issues.sh`, `docs/status/BUCC_TODO.md` |
| P1 | 100% | Complete | `SHARED.CAS` uses deep value equality for structured arrays/maps while preserving scalar behavior. | `src/buccaneer/host.c`, `src/buccaneer/value.c`, `tests/test_buccaneer_host.c` |
| P1 | 100% | Complete | Release/update tooling requires configured metadata, verifies SHA256 before extraction, rejects unsafe archive entries, and supports no-network dry-run checks. | `scripts/update-version` |

## Validation Snapshot

- `make test` passed: 46/46 tests
- `make all`, `make tools`, `make plank`, `make plugins`, `make bucc`, and `make dist-buccaneer` passed from `build-make/`
