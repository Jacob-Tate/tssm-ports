#!/usr/bin/env bash
# Builds the relay server and test client natively (Linux/WSL), checks that protocol.h compiles as C99
# and as C++, starts the server on a free port and runs every test_client scenario against it.
# Usage: tools/server_test.sh      (exit 0 = all passed)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/server"

echo "== protocol.h as C99 and C++17"
mkdir -p "$ROOT/build/proto_inc/netplay"
# Only the protocol header on the include path: the repository's include/ has game headers that shadow system ones.
cp "$ROOT/include/netplay/protocol.h" "$ROOT/build/proto_inc/netplay/protocol.h"
printf '#include "netplay/protocol.h"\nint main(void) { return 0; }\n' > "$ROOT/build/proto_check.c"
cp "$ROOT/build/proto_check.c" "$ROOT/build/proto_check.cpp"
if command -v gcc >/dev/null 2>&1; then
    gcc -std=c99 -Wall -Wextra -pedantic -Werror -I"$ROOT/build/proto_inc" -fsyntax-only "$ROOT/build/proto_check.c"
else echo "(gcc not found, skipping the C99 check)"; fi
if command -v g++ >/dev/null 2>&1; then
    g++ -std=c++17 -Wall -Wextra -pedantic -Werror -I"$ROOT/build/proto_inc" -fsyntax-only "$ROOT/build/proto_check.cpp"
else echo "(g++ not found, skipping the C++17 check)"; fi

echo "== build"
cmake -S "$ROOT/server" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$ROOT/build/server_configure.log" 2>&1 \
    || { cat "$ROOT/build/server_configure.log"; exit 1; }
cmake --build "$BUILD" 2>&1 | tee "$ROOT/build/server_build.log"
if grep -qi "warning" "$ROOT/build/server_build.log"; then echo "FAIL: build warnings"; exit 1; fi

echo "== run"
PORT="${TSSM_TEST_PORT:-$((20000 + RANDOM % 20000))}"
LOG="$ROOT/build/server_run.log"
"$BUILD/tssm-server" --port "$PORT" > "$LOG" 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true; wait $SERVER_PID 2>/dev/null || true' EXIT
sleep 0.5
kill -0 $SERVER_PID 2>/dev/null || { echo "server failed to start:"; cat "$LOG"; exit 1; }

RC=0
"$BUILD/tssm-server-test" 127.0.0.1 "$PORT" || RC=$?
if [ $RC -ne 0 ]; then echo "--- server log (tail) ---"; tail -n 40 "$LOG"; fi
exit $RC
