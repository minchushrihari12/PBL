#!/bin/sh
# Builds everything, starts core + logger + web server. Ctrl+C stops all of them.
cd "$(dirname "$0")"
make || exit 1
trap 'kill 0' INT TERM EXIT
./core &
./logger &
sleep 1
./server
