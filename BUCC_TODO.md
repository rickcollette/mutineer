# BUCC_TODO — Buccaneer Follow-Up

Current status is tracked in `feature-matrix.md`, which is the source of truth
for completed Buccaneer work. The previous audit items for chain/exit status,
host API parity, runtime hardening, ON CALL/range emission, CMake integration, BBS
launch, capability enforcement, and embedding context defaults have been
implemented and covered by the current CTest suite.

## Current Follow-Up Candidates

- `USER.FLAGS` has host API structure support but is not part of the documented
  door-author API yet; either dispatch and document it or keep it internal.
- Convenience setters for broader USERS/MSG/FILE host APIs can be added when a
  real Buccaneer package needs those surfaces.

## Validation References

- `tests/test_buccaneer_host.c`
- `tests/test_buccaneer_vm.c`
- `tests/test_buccaneer_bbs.c`
- `tests/test_next15_matrix.c`
