#!/bin/bash
# Full-system test with the starter UI/Core/Logger (scripted run, 19 commands).  make test-starters
cd "$(dirname "$0")/.." || exit 1
rm -f sim.log
out=$(UI_AUTO=1 timeout 90 ./integrator 2>&1); rc=$?
echo "$out"; echo "-----"
pass=0; fail=0
check() { if eval "$2"; then echo "PASS: $1"; pass=$((pass+1)); else echo "FAIL: $1"; fail=$((fail+1)); fi; }

check "T1 integrator exits cleanly (rc=0, no hang)"       '[ $rc -eq 0 ]'
check "T2 all 3 processes connected over socket"           '[ "$(echo "$out" | grep -c " ready ")" -eq 3 ]'
check "T3 UI->Core (mq): 19/19 replies, 1 expected error"  'echo "$out" | grep -q "UI: replies 19/19, errors=1"'
check "T4 Core state in shm seen by UI (pc/acc/sp/queue)"  'echo "$out" | grep -q "UI: snapshot pc=3 acc=5 sp=3 qcount=3 mem0=5"'
check "T5 Core->Logger (pipe): 19 log lines in sim.log"    '[ "$(wc -l < sim.log 2>/dev/null)" -eq 19 ]'
check "T6 error logged exactly once"                       '[ "$(grep -c "|ERROR|" sim.log)" -eq 1 ]'
check "T7 Logger summary matches"                          'echo "$out" | grep -q "LOGGER: 19 lines, 1 errors"'
check "T8 metrics received from all 3"                     '[ "$(echo "$out" | grep -c "^\[metrics\]")" -eq 3 ]'
check "T9 no leftover IPC objects"                         '[ ! -e /dev/shm/sim_shm ] && [ ! -e /tmp/sim_ctrl.sock ] && [ ! -e /tmp/sim_log.fifo ]'

echo "Passed $pass, failed $fail"
[ $fail -eq 0 ]
