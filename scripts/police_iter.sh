#!/usr/bin/env bash
# One iteration of the ChoosePoliceCarModel differential-test loop, on CT 103.
#
# Unlike the bike loop, the branch arms here are NOT a tail event to be waited
# out: AreSwatRequired/AreFbiRequired/AreArmyRequired key off m_nWantedLevel,
# which the test writes directly. The only thing that must be waited on is
# STREAMING -- the guards also require the cop vehicle AND its ped crew loaded.
# So the lever is the model list + warmup frames, not repetition.
#
# Usage: police_iter.sh "<MODEL_LIST>" "<TIMEOUT_S>" "<WARMUP_FRAMES>"
set -u
cd /opt/difftest/gta-reversed-diff-test || exit 3

# vehicles: 427 enforcer 432 rhino 433 barracks 490 fbiranch 523 copbike
#           596-599 copcars
# peds:     280 cop 285 swat 286 fbi 287 army 300-space cop biker is 284
MODELS="${1:-280,281,282,283,284,285,286,287,427,432,433,490,523,596,597,598,599}"
TIMEOUT="${2:-300}"
WARMUP="${3:-600}"
PR="/opt/difftest/gta-reversed-diff-test"
LOGS="/tmp/wine-logs"
RESULTS="${LOGS}/game_test_results.txt"
IMAGE="gta-reversed-build"

rm -f "${RESULTS}"

docker run --rm \
  -v "${PR}/GTASA:/game:ro" \
  -v "${PR}/build-output:/build:ro" \
  -v "${PR}/gamebin/gta_sa_compact.exe:/gamebin/gta_sa_compact.exe:ro" \
  -v "${PR}/scripts:/scripts:ro" \
  -v "${PR}/configs:/configs:ro" \
  -v "${LOGS}:/tmp/wine-logs" \
  -e GAME_TEST_ENABLE=1 \
  -e GAME_TEST_FILTER=CCarCtrl \
  -e GAME_TEST_REQUEST_MODELS="${MODELS}" \
  -e GAME_TEST_WARMUP_FRAMES="${WARMUP}" \
  -e TIMEOUT="${TIMEOUT}" \
  "${IMAGE}" bash -c '/scripts/run-headless.sh > /tmp/wine-logs/police-run.log 2>&1; cp /opt/wine-gtasa/drive_c/*.txt /tmp/wine-logs/ 2>/dev/null; cp /opt/wine-gtasa/drive_c/Games/GTASA/*.txt /tmp/wine-logs/ 2>/dev/null; true' \
  >/dev/null 2>&1

# The authoritative verdict is the PASS/FAIL assertion line + STATUS, NOT the
# printf summary -- printf does not reach the results file (learned in the bike
# run, which misread a printf-less result as infra failure).
POL=$(grep -aE "(PASS|FAIL)  CCarCtrl/Diff_ChoosePoliceCarModel" "${RESULTS}" 2>/dev/null | tail -1)
STATUS=$(grep -aoE "STATUS=[A-Z]+" "${RESULTS}" 2>/dev/null | tail -1)
PASSED=$(grep -aoE "PASSED=[0-9]+" "${RESULTS}" 2>/dev/null | tail -1)
FAILED=$(grep -aoE "FAILED=[0-9]+" "${RESULTS}" 2>/dev/null | tail -1)

# printf diagnostics land in the run log even though not in the results file.
SUM=$(grep -haoE "\[ChoosePoliceCarModel\] compared=[0-9]+ mismatch=[0-9]+ nondefault=[0-9]+ enforcer=[0-9]+ fbi=[0-9]+ army=[0-9]+ distinct=[0-9]+" "${LOGS}/police-run.log" "${RESULTS}" 2>/dev/null | tail -1)
LOADED=$(grep -haoE "\[ChoosePoliceCarModel\] loaded: [^\"]*" "${LOGS}/police-run.log" "${RESULTS}" 2>/dev/null | tail -1)
CRASH=$(grep -aciE "page fault|unhandled exception" "${LOGS}/police-run.log" 2>/dev/null)

echo "POLICE_LINE : ${POL:-<none>}"
echo "STATUS      : ${STATUS:-<none>}  ${PASSED:-} ${FAILED:-}"
echo "SUMMARY     : ${SUM:-<none>}"
echo "LOADED      : ${LOADED:-<none>}"
echo "CRASH       : ${CRASH:-0}"
echo "--- failures (if any) ---"
grep -aE "^FAIL|ChoosePoliceCarModel" "${RESULTS}" 2>/dev/null | head -20
