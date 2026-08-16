#!/usr/bin/env bash
# One batched differential-test run: many tests, one game boot.
# The 4-minute cost is boot + warmup streaming; the tests themselves are
# milliseconds, so batching is nearly free. Models are the one shared resource,
# so REQUEST_MODELS is the union of what every test in the batch needs.
set -u
cd /opt/difftest/gta-reversed-diff-test || exit 3

# union: cop peds+cars (police/cop-model tests), gang+generic vehicles
# (ChooseCarModelToLoad, PickRiotRoadBlockCar)
MODELS="${1:-280,281,282,283,284,285,286,287,427,432,433,490,523,596,597,598,599,400,401,402,404,410,412,415,418,420,426,436,438,439,445,451,466,467,492,507,516,517,518,527,529,533,534,535,536,540,541,542,543,545,546,547,549,550,551,552,554,555,558,559,560,561,562,565,566,567,575,576,579,580,585,587,589,600,602,603}"
TIMEOUT="${2:-500}"
WARMUP="${3:-900}"
FILTER="${4:-CCarCtrl,CStreaming,CPopulation}"
PR="/opt/difftest/gta-reversed-diff-test"
LOGS="/tmp/wine-logs"
RESULTS="${LOGS}/game_test_results.txt"

rm -f "${RESULTS}"

docker run --rm \
  -v "${PR}/GTASA:/game:ro" \
  -v "${PR}/build-output:/build:ro" \
  -v "${PR}/gamebin/gta_sa_compact.exe:/gamebin/gta_sa_compact.exe:ro" \
  -v "${PR}/scripts:/scripts:ro" \
  -v "${PR}/configs:/configs:ro" \
  -v "${LOGS}:/tmp/wine-logs" \
  -e GAME_TEST_ENABLE=1 \
  -e GAME_TEST_FILTER="${FILTER}" \
  -e GAME_TEST_REQUEST_MODELS="${MODELS}" \
  -e GAME_TEST_WARMUP_FRAMES="${WARMUP}" \
  -e TIMEOUT="${TIMEOUT}" \
  gta-reversed-build bash -c '/scripts/run-headless.sh > /tmp/wine-logs/batch-run.log 2>&1; cp /opt/wine-gtasa/drive_c/*.txt /tmp/wine-logs/ 2>/dev/null; cp /opt/wine-gtasa/drive_c/Games/GTASA/*.txt /tmp/wine-logs/ 2>/dev/null; true' \
  >/dev/null 2>&1

echo "=== BATCH TESTS (new this round) ==="
grep -aE "(PASS|FAIL|INFO)  (CCarCtrl/Diff_ChooseCarModelToLoad_Seeded|CCarCtrl/Diff_ChoosePoliceCarModel_WantedDriven|CStreaming/Diff_GetDefaultCop|CPopulation/Diff_PickRiotRoadBlockCar)" "${RESULTS}" 2>/dev/null
echo "=== ALL FAILURES ==="
grep -aE "^  FAIL" "${RESULTS}" 2>/dev/null | head -20
echo "=== TOTALS ==="
grep -aoE "PASSED=[0-9]+|FAILED=[0-9]+|STATUS=[A-Z]+" "${RESULTS}" 2>/dev/null | tail -3
