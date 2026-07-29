#!/usr/bin/env bash
# Copy the C++ core into vendor/ so the published crate can build itself.
#
# crates.io packages must be self-contained: build.rs compiles the core with
# CMake, so those sources have to be inside the .crate file. This script is run
# before `cargo publish`, and vendor/ is gitignored — it is a build product, not
# source, and committing it would duplicate the whole core in every diff.
#
# ONNX Runtime is deliberately NOT vendored. It is ~17 MB per platform, carries
# its own floors (macOS arm64 needs 13.3, Linux glibc 2.28), and bundling four
# copies inside a crate would inherit all of that silently. Callers point at it
# with ONNXRUNTIME_ROOT.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
dest="$here/vendor"

rm -rf "$dest"
mkdir -p "$dest"

# Only what CMake needs to build naina-core. No tests, no other bindings, no app.
cp "$repo/CMakeLists.txt" "$dest/"
cp -R "$repo/core" "$dest/core"
cp -R "$repo/cmake" "$dest/cmake"
mkdir -p "$dest/models"
cp "$repo/models/registry.yaml" "$dest/models/"
cp "$repo/models/manifest.schema.json" "$dest/models/"
rm -rf "$dest/core/tests"

# The root CMakeLists adds subdirectories that are not vendored. Neutralise them
# rather than editing the real file.
python3 - "$dest/CMakeLists.txt" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1]); s = p.read_text()
for opt in ("NAINA_BUILD_TESTS", "NAINA_BUILD_PYTHON", "NAINA_BUILD_WASM"):
    s = s.replace(f'option({opt}', f'# vendored: forced OFF\nset({opt} OFF CACHE BOOL "" FORCE)\noption({opt}')
p.write_text(s)
PY

echo "vendored $(du -sh "$dest" | cut -f1) into $dest"
echo "next: (cd $here && cargo package --allow-dirty --list | head)"
