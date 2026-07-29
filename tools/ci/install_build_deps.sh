#!/usr/bin/env bash
# Build dependencies for a manylinux wheel build.
#
# A script rather than a cibuildwheel one-liner, because the one-liner it replaces
# had a precedence bug that only failed inside the container:
#
#   yum install -y A || apt-get update && apt-get install -y B
#
# sh parses that as `(yum || apt-get update) && apt-get install`, so apt-get ran
# even when yum had already succeeded — and manylinux images have no apt-get.
# Exit 127, every wheel job red.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ONNX Runtime first: the wheel is useless without an inference backend.
bash "${here}/install_onnxruntime.sh"

# libcurl development headers, for the model downloader. manylinux2014 is
# CentOS-based (yum); manylinux_2_28 is AlmaLinux (dnf); a Debian-based image
# would need apt-get. Pick by what exists rather than by chaining fallbacks.
if command -v dnf >/dev/null 2>&1; then
    dnf install -y libcurl-devel
elif command -v yum >/dev/null 2>&1; then
    yum install -y libcurl-devel
elif command -v apt-get >/dev/null 2>&1; then
    apt-get update
    apt-get install -y libcurl4-openssl-dev
else
    echo "install_build_deps: no supported package manager (dnf/yum/apt-get)" >&2
    exit 1
fi

# Fail here rather than at the CMake step, where the message is less obvious.
if ! find /usr/include /usr/local/include -name 'curl.h' -print -quit | grep -q .; then
    echo "install_build_deps: curl.h not found after install" >&2
    exit 1
fi
echo "install_build_deps: ok"
