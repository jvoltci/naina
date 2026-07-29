#!/usr/bin/env python3
"""Assert the installed naina wheel carries no build-machine library paths.

This exists because a wheel that imports fine on the build machine can fail on
every user's machine. naina's extension once linked
``/opt/homebrew/opt/onnxruntime/lib/libonnxruntime.1.dylib`` — present on the
builder, absent for anyone without that exact Homebrew install, including every
Intel Mac where the prefix differs.

``delocate`` / ``auditwheel`` are supposed to rewrite those load paths to point
inside the wheel. This verifies they actually did, rather than trusting them.

Run inside cibuildwheel's test environment, where only the wheel is installed.
"""

from __future__ import annotations

import pathlib
import platform
import subprocess
import sys

# Absolute prefixes that must never appear in a shipped binary's load commands.
# Anything here means the wheel depends on a path from the build machine.
FORBIDDEN_PREFIXES = (
    "/opt/homebrew",
    "/usr/local/Cellar",
    "/usr/local/opt",
    "/tmp/onnxruntime",
    "/home/",
    "/Users/",
    "/project/",
    "/io/",
)


def extension_paths() -> list[pathlib.Path]:
    import naina

    pkg = pathlib.Path(naina.__file__).resolve().parent
    suffixes = (".so", ".dylib", ".pyd", ".dll")
    return sorted(p for p in pkg.rglob("*") if p.suffix in suffixes)


def load_commands(path: pathlib.Path) -> list[str]:
    system = platform.system()
    if system == "Darwin":
        out = subprocess.run(["otool", "-L", str(path)], capture_output=True, text=True)
        if out.returncode != 0:
            return []
        # Skip the first line (the file's own name).
        return [ln.strip().split(" ")[0] for ln in out.stdout.splitlines()[1:] if ln.strip()]
    if system == "Linux":
        out = subprocess.run(["ldd", str(path)], capture_output=True, text=True)
        if out.returncode != 0:
            return []
        deps = []
        for ln in out.stdout.splitlines():
            parts = ln.split("=>")
            if len(parts) == 2:
                deps.append(parts[1].strip().split(" ")[0])
            else:
                deps.append(ln.strip().split(" ")[0])
        return [d for d in deps if d]
    # Windows: no equivalent standard tool available here. The import check
    # below is still meaningful, so do not fail the build over it.
    return []


def main() -> int:
    # 1) The wheel must import at all. This alone catches the classic failure.
    try:
        import naina
    except Exception as e:  # noqa: BLE001
        print(f"FAIL import naina: {e}", file=sys.stderr)
        return 1
    print(f"import naina: ok (version {naina.__version__})")

    binaries = extension_paths()
    if not binaries:
        print("FAIL no extension module found in the installed package", file=sys.stderr)
        return 1

    problems = 0
    for path in binaries:
        deps = load_commands(path)
        print(f"\n{path.name}")
        if not deps:
            print("  (no dependency listing available on this platform)")
            continue
        for dep in deps:
            bad = any(dep.startswith(p) for p in FORBIDDEN_PREFIXES)
            if bad:
                print(f"  FORBIDDEN  {dep}")
                problems += 1
            else:
                print(f"  ok         {dep}")

    # 2) yaml-cpp is statically linked, so it must not appear as a shared dep.
    all_deps = " ".join(d for p in binaries for d in load_commands(p))
    if "yaml-cpp" in all_deps:
        print("\nFAIL yaml-cpp appears as a shared dependency; it should be static",
              file=sys.stderr)
        problems += 1

    if problems:
        print(f"\n{problems} problem(s): the wheel is NOT self-contained", file=sys.stderr)
        return 1
    print("\nwheel is self-contained")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
