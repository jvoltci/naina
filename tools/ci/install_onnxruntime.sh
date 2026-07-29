#!/usr/bin/env bash
# Install ONNX Runtime from the official upstream release into /tmp/onnxruntime.
#
# Why not a package manager: cibuildwheel's Linux builds run in a manylinux
# container that has no onnxruntime package at all, and on macOS relying on
# Homebrew ties the wheel to the runner's CPU architecture and prefix. Taking
# the upstream tarball makes the build identical everywhere and pins the exact
# version the wheel is tested against.
#
# FindONNXRuntime.cmake honours ONNXRUNTIME_ROOT, which the cibuildwheel
# environment sets to this prefix.
set -euo pipefail

ORT_VERSION="${ORT_VERSION:-1.20.1}"
PREFIX="${ONNXRUNTIME_ROOT:-/tmp/onnxruntime}"

if [ -f "$PREFIX/include/onnxruntime_cxx_api.h" ]; then
    echo "onnxruntime already present at $PREFIX"
    exit 0
fi

uname_s="$(uname -s)"
uname_m="$(uname -m)"

case "$uname_s" in
    Darwin)
        # The macOS build is a universal2 archive covering both arm64 and x86_64,
        # so cross-compiling either slice works from one download.
        asset="onnxruntime-osx-universal2-${ORT_VERSION}.tgz"
        ;;
    Linux)
        case "$uname_m" in
            x86_64)          asset="onnxruntime-linux-x64-${ORT_VERSION}.tgz" ;;
            aarch64|arm64)   asset="onnxruntime-linux-aarch64-${ORT_VERSION}.tgz" ;;
            *) echo "unsupported Linux arch: $uname_m" >&2; exit 1 ;;
        esac
        ;;
    *)
        echo "unsupported OS: $uname_s" >&2
        exit 1
        ;;
esac

url="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${asset}"
echo "fetching $url"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# curl is present in manylinux and on macOS runners; wget is not guaranteed.
curl -fsSL --retry 3 -o "$tmp/ort.tgz" "$url"
mkdir -p "$PREFIX"
tar -xzf "$tmp/ort.tgz" -C "$PREFIX" --strip-components=1

test -f "$PREFIX/include/onnxruntime_cxx_api.h" \
    || { echo "extract failed: no headers under $PREFIX" >&2; exit 1; }
ls "$PREFIX"/lib/libonnxruntime* >/dev/null \
    || { echo "extract failed: no library under $PREFIX/lib" >&2; exit 1; }

echo "onnxruntime ${ORT_VERSION} installed at $PREFIX"
