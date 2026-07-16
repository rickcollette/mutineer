#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ARTIFACTS=${MTS_READY_ARTIFACTS:-$ROOT/build-mts-ready-artifacts}
BUILD=$ROOT/build-mts-ready
ASAN=$ROOT/build-mts-ready-asan
TSAN=$ROOT/build-mts-ready-tsan
mkdir -p "$ARTIFACTS"
summary="$ARTIFACTS/summary.json"
status=failed
trap 'printf "{\"status\":\"%s\",\"artifacts\":\"%s\"}\n" "$status" "$ARTIFACTS" > "$summary"' EXIT

command -v expect >/dev/null
command -v clang-18 >/dev/null

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DMTS_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" -j2 2>&1 | tee "$ARTIFACTS/build.log"
for run in 1 2 3; do
  ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tee "$ARTIFACTS/ctest-$run.log"
done

for run in 1 2 3; do
  MTS_STRESS_ROUNDS=100 MTS_STRESS_OPS=1000 \
    "$BUILD/test_mts_concurrency" 2>&1 | tee "$ARTIFACTS/native-stress-$run.log"
done

"$ROOT/tests/run_expect_tests.sh" "$BUILD" test_mts_three_client.exp \
  2>&1 | tee "$ARTIFACTS/expect.log"

cmake -S "$ROOT" -B "$ASAN" -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DMTS_WARNINGS_AS_ERRORS=ON
cmake --build "$ASAN" --target test_mts test_mts_concurrency -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --test-dir "$ASAN" \
  -R '^mts(_concurrency)?$' --output-on-failure 2>&1 | tee "$ARTIFACTS/asan.log"

CC=clang-18 cmake -S "$ROOT" -B "$TSAN" -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_TSAN=ON -DMTS_WARNINGS_AS_ERRORS=ON
cmake --build "$TSAN" --target test_mts_concurrency -j2
MTS_STRESS_ROUNDS=25 MTS_STRESS_OPS=500 \
TSAN_OPTIONS=halt_on_error=1:history_size=7 \
  "$TSAN/test_mts_concurrency" 2>&1 | tee "$ARTIFACTS/tsan.log"

ctest --test-dir "$BUILD" -R '^(plugin|mts_docs_consistency)$' --output-on-failure

status=passed
printf '{"status":"passed","full_suite_runs":3,"native_stress_runs":3,"asan":"passed","tsan":"passed","expect":"passed","plugin":{"id":"com.mutineer.chat","name":"Mutineer Teleconference System","version":"2.0.0"},"artifacts":"%s","compiler":"%s"}\n' \
  "$ARTIFACTS" "$(clang-18 --version | head -1)" > "$summary"
trap - EXIT
echo "MTS READY: YES"
