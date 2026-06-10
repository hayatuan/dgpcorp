#!/usr/bin/env python3
"""Tạo compile_flags.txt fallback cho clangd (header-only / khi DB chưa load)."""
from __future__ import annotations

import json
import shlex
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: generate_compile_flags.py <compile_commands.json> <out compile_flags.txt>", file=sys.stderr)
        return 1

    db_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    root = out_path.parent.resolve()

    data = json.loads(db_path.read_text(encoding="utf-8"))
    entry = next((e for e in data if e.get("file", "").endswith("Main.cpp")), data[0] if data else None)
    if entry is None:
        print("empty compile database", file=sys.stderr)
        return 1

    cmd = entry.get("command") or ""
    parts = shlex.split(cmd)

    skip_prefixes = ("-o", "-c", "-M", "-MF", "-MT", "-MQ")
    flags: list[str] = []

    for part in parts[1:]:
        if part.endswith((".cpp", ".mm", ".o", ".obj")):
            continue
        if any(part.startswith(p) for p in skip_prefixes):
            continue
        if part in ("-g", "-O0", "-O1", "-O2", "-O3"):
            continue

        if part.startswith("-I") and len(part) > 2:
            inc = Path(part[2:])
            try:
                rel = inc.resolve().relative_to(root)
                flags.append("-I" + str(rel))
                continue
            except ValueError:
                pass

        flags.append(part)

    if "-std=gnu++17" not in flags and "-std=c++17" not in flags:
        flags.insert(0, "-std=gnu++17")

    out_path.write_text("\n".join(flags) + "\n", encoding="utf-8")
    print(f"wrote {out_path} ({len(flags)} flags)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
