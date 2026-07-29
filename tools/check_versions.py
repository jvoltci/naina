#!/usr/bin/env python3
"""Assert every declared version of naina agrees.

naina states its version in four independent places, and they silently drifted
once already (CMake and pyproject said 0.1.0 while naina.h and package.json
said 0.2.0). Publishing from that state produces a wheel whose metadata
disagrees with the ABI it contains, which is the kind of thing nobody notices
until a bug report is impossible to interpret.

Run standalone, or in CI:

    python tools/check_versions.py
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text()
    m = re.search(r"^\s*VERSION\s+(\d+\.\d+\.\d+)", text, re.M)
    if not m:
        sys.exit("CMakeLists.txt: no project VERSION found")
    return m.group(1)


def c_abi_version() -> str:
    text = (ROOT / "core/include/naina/naina.h").read_text()
    parts = []
    for field in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(rf"#define NAINA_VERSION_{field}\s+(\d+)", text)
        if not m:
            sys.exit(f"naina.h: NAINA_VERSION_{field} not found")
        parts.append(m.group(1))
    return ".".join(parts)


def pyproject_version() -> str:
    text = (ROOT / "pyproject.toml").read_text()
    m = re.search(r'^version\s*=\s*"([^"]+)"', text, re.M)
    if not m:
        sys.exit("pyproject.toml: no version found")
    return m.group(1)


def npm_version() -> str:
    return json.loads((ROOT / "bindings/node/package.json").read_text())["version"]


def main() -> int:
    found = {
        "CMakeLists.txt (project VERSION)": cmake_version(),
        "core/include/naina/naina.h (C ABI macros)": c_abi_version(),
        "pyproject.toml (wheel metadata)": pyproject_version(),
        "bindings/node/package.json (npm)": npm_version(),
    }
    width = max(len(k) for k in found)
    for source, version in found.items():
        print(f"  {source:<{width}}  {version}")

    distinct = set(found.values())
    if len(distinct) != 1:
        print(f"\nVERSION MISMATCH: {sorted(distinct)}", file=sys.stderr)
        print("All four must agree before publishing.", file=sys.stderr)
        return 1
    print(f"\nall agree: {distinct.pop()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
