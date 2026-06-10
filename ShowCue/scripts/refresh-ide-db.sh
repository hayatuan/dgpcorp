#!/usr/bin/env bash
# Tạo/cập nhật compile_commands.json cho clangd (Cursor Problems).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target sync_clangd_db

for f in build/compile_commands.json compile_commands.json; do
  if [[ ! -s "$f" ]]; then
    echo "ERROR: $f empty — chạy lại: cmake -B build && cmake --build build" >&2
    exit 1
  fi
done

python3 scripts/generate_compile_flags.py build/compile_commands.json compile_flags.txt
echo "OK: compile_commands.json ($(wc -c < compile_commands.json) bytes, $(python3 -c "import json;print(len(json.load(open('compile_commands.json'))))") entries)"
echo "OK: compile_flags.txt ($(wc -c < compile_flags.txt) bytes)"
echo "→ Cursor: Command Palette → 'clangd: Restart language server'"
