#!/bin/bash
# IPC test cases. Run from repo root: make test   (use `make real` first to test real code)
cd "$(dirname "$0")/.." || exit 1
N=$(grep N_CMDS ipc_defs.h | awk '{print $3}')
rm -f sim.log
out=$(timeout 90 ./integrator 2>&1); rc=$?
echo "$out"; echo "-----"
pass=0; fail=0
check() { if eval "$2"; then echo "PASS: $1"; pass=$((pass+1)); else echo "FAIL: $1"; fail=$((fail+1)); fi; }

check "T1 integrator exits cleanly (rc=0, no hang)"   '[ $rc -eq 0 ]'
check "T2 all 3 processes connected over socket"       '[ "$(echo "$out" | grep -c " ready ")" -eq 3 ]'
check "T3 UI->Core mq: every command got a reply"      'echo "$out" | grep -q "$N/$N replies ok"'
check "T4 Core shm: UI sees counter == $N"             'echo "$out" | grep -q "shm counter=$N"'
check "T5 Core->Logger pipe: $N log lines written"     '[ "$(wc -l < sim.log 2>/dev/null)" -eq "$N" ]'
check "T6 log order preserved (first/last line)"       '[ "$(head -1 sim.log)" = "CORE|INFO|exec cmd 1" ] && [ "$(tail -1 sim.log)" = "CORE|INFO|exec cmd $N" ]'
check "T7 metrics received from all 3"                 '[ "$(echo "$out" | grep -c "^\[metrics\]")" -eq 3 ]'
check "T8 no leftover IPC objects"                     '[ ! -e /dev/shm/sim_shm ] && [ ! -e /tmp/sim_ctrl.sock ] && [ ! -e /tmp/sim_log.fifo ]'

echo "Passed $pass, failed $fail"
[ $fail -eq 0 ]
