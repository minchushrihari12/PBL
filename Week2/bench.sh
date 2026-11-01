#!/bin/bash
# Standalone vs multi-process benchmark. Usage: bash tests/bench.sh [runs]
cd "$(dirname "$0")/.." || exit 1
RUNS=${1:-10}
N=$(grep N_CMDS ipc_defs.h | awk '{print $3}')
sa=(); mp=(); lat=()
for i in $(seq "$RUNS"); do
  sa+=( "$(./standalone | awk '/total run/{print $4}')" )
  o=$(./integrator)
  mp+=( "$(echo "$o" | awk '/total run/{print $4}')" )
  lat+=( "$(echo "$o" | awk '/UI/ && /ipc_latency/{split($3,a,"="); print a[2]}')" )
  [ "$i" -eq "$RUNS" ] && last="$o"
done
avg() { printf '%s\n' "$@" | awk '{s+=$1} END{printf "%.2f", s/NR}'; }
{
echo "Benchmark: $N commands, $RUNS runs each"
echo "Standalone avg total time : $(avg "${sa[@]}") ms"
echo "Multi-process avg total   : $(avg "${mp[@]}") ms"
echo "Avg IPC round trip (UI<->Core, mq): $(avg "${lat[@]}") us"
echo "--- last multi-process run (CPU / memory per process) ---"
echo "$last" | grep "^\[metrics\]"
./standalone | tail -1
} | tee benchmark_results.txt
