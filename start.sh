#!/bin/sh
# RPS Arena — build everything and open the UI (macOS / Linux)
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Python 3 is required — install it with your package manager (or from python.org)."
  exit 1
fi
exec python3 start.py "$@"
