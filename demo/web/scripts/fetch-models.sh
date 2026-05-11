#!/usr/bin/env bash
# Download face detection + recognition ONNX models into public/models/.
# Run automatically before `vite dev` / `vite build` via npm scripts.
#
# Models:
#   yunet.onnx  ~337 KB  MIT       (OpenCV Zoo)
#   sface.onnx  ~37  MB  Apache-2  (OpenCV Zoo)
#
# These are the same model artifacts the native naina lib will load
# (see ../../models/registry.yaml).
set -euo pipefail

cd "$(dirname "$0")/.."

DEST="public/models"
mkdir -p "$DEST"

YUNET_URL="https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx"
SFACE_URL="https://github.com/opencv/opencv_zoo/raw/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx"

fetch() {
  local url="$1"
  local out="$2"
  if [ -s "$out" ]; then
    echo "ok: $out exists (skip)"
    return
  fi
  echo "fetching: $url"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "$out" "$url"
  else
    wget -O "$out" "$url"
  fi
  echo "ok: $out"
}

fetch "$YUNET_URL" "$DEST/yunet.onnx"
fetch "$SFACE_URL" "$DEST/sface.onnx"

echo "Models ready in $DEST/"
