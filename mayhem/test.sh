#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# 1) Upstream's gtest suites for the live-config INI and static display config code
#    (tests/miral/{live_config,live_config_ini_file,static_display_config}.cpp), built by build.sh
#    with normal flags.
# 2) A known-answer check of src/miral/xcursor.c: load upstream's testing cursor theme
#    (tests/mir_test_framework/testing-cursor-theme, generated from *.in = "24 0 0 <colour>.png")
#    and diff the decoded size/hotspot/pixels against mayhem/xcursor_selftest.expected.
T="$SRC/mayhem/out/test"
for b in miral-config-tests xcursor-selftest; do
  [ -x "$T/$b" ] || { echo "test.sh: $T/$b missing — run mayhem/build.sh first" >&2; emit_ctrf gtest 0 1 0; exit 1; }
done

passed=0 failed=0 skipped=0
log="$(mktemp)"
"$T/miral-config-tests" >"$log" 2>&1
rc=$?
cat "$log"
ran="$(sed -n 's/^\[==========\] \([0-9]*\) tests\? from .* ran\..*/\1/p' "$log" | tail -1)"
gp="$(sed -n 's/^\[  PASSED  \] \([0-9]*\) test.*/\1/p' "$log" | tail -1)"
gs="$(sed -n 's/^\[  SKIPPED \] \([0-9]*\) test.*/\1/p' "$log" | tail -1)"
ran="${ran:-0}" gp="${gp:-0}" gs="${gs:-0}"
gf=$(( ran - gp - gs ))
# A suite that ran nothing, or exited non-zero without reporting a failure, is a failure.
if [ "$ran" -eq 0 ] || { [ "$rc" -ne 0 ] && [ "$gf" -le 0 ]; }; then gf=$(( gf + 1 )); fi
passed=$(( passed + gp )); failed=$(( failed + gf )); skipped=$(( skipped + gs ))
rm -f "$log"

for size in 24 32; do
  got="$(XCURSOR_PATH="$SRC/tests/mir_test_framework/testing-cursor-theme" "$T/xcursor-selftest" "$size" | LC_ALL=C sort)"
  if [ "$got" = "$(cat "$SRC/mayhem/xcursor_selftest.expected")" ]; then
    passed=$(( passed + 1 )); echo "PASS xcursor known-answer (size $size)"
  else
    failed=$(( failed + 1 )); echo "FAIL xcursor known-answer (size $size)"; diff <(echo "$got") "$SRC/mayhem/xcursor_selftest.expected"
  fi
done

emit_ctrf gtest "$passed" "$failed" "$skipped"
