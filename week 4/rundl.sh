#!/usr/bin/env bash
# Starts core -> logger -> ui, opens the GUI, stops everything on Ctrl+C.
cd "$(dirname "$0")" || exit 1
PORT="${1:-8085}"

make -s || { echo "Build failed"; exit 1; }

./core & CORE=$!
sleep 0.5
./logger & LOG=$!
./ui "$PORT" & UI=$!

cleanup() {
    kill "$UI" "$LOG" 2>/dev/null
    kill "$CORE" 2>/dev/null          # core removes the message queues on exit
    wait 2>/dev/null
    exit 0
}
trap cleanup INT TERM

sleep 0.7
URL="http://127.0.0.1:$PORT"
case "$(uname)" in
    Darwin) open "$URL" ;;
    *) if command -v xdg-open >/dev/null 2>&1; then xdg-open "$URL" >/dev/null 2>&1; fi ;;
esac
echo
echo "GUI: $URL     (Ctrl+C to stop)"
wait
