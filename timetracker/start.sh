#!/usr/bin/env bash
# Runs the app from this folder (e.g. on a shared drive) using PHP's built-in server.
# Only this computer can reach it. Needs PHP 8.1+ with the pdo_sqlite extension.
cd "$(dirname "$0")"
PORT="${PORT:-8765}"
URL="http://127.0.0.1:$PORT/"
echo "Timesheets running at $URL  (press Ctrl+C to stop)"
(
    sleep 1
    if command -v xdg-open >/dev/null; then xdg-open "$URL"; elif command -v open >/dev/null; then open "$URL"; fi
) >/dev/null 2>&1 &
exec php -S "127.0.0.1:$PORT" router.php
