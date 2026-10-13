#!/bin/bash
# Starts Logger and Core in the background, then the UI in the foreground.
cd "$(dirname "$0")"
rm -f /dev/mqueue/sim_*          # remove leftover queues from earlier runs
./logger &
LOGGER_PID=$!
./core &
CORE_PID=$!
trap 'kill $LOGGER_PID $CORE_PID 2>/dev/null; rm -f /dev/mqueue/sim_*' INT TERM
./ui
wait $CORE_PID $LOGGER_PID 2>/dev/null
rm -f /dev/mqueue/sim_*
