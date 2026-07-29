#!/usr/bin/env python3
"""Mirror upstream model weights into naina's own GitHub Release.

Why this exists
---------------
naina originally fetched weights straight from the Hugging Face Hub. The
sha256 in ``models/registry.yaml`` means an upstream file being swapped fails
closed rather than corrupting output — but it still *breaks*. Mirroring the
artifacts into naina's own release turns "breaks loudly one day" into "never
breaks", and removes a third-party runtime dependency.

The weights stay out of git (they are ~306 MB and ``.gitignore`` excludes
``*.onnx``); only the release carries them.

Licensing
---------
Every mirrored model is Apache-2.0, which permits redistribution provided the
licence and attribution travel with it. See the repository ``NOTICE`` file.
Each manifest entry keeps a ``source_url`` recording exactly where the bytes
came from, so provenance stays auditable.

Usage
-----
    python tools/mirror_models.py fetch     # download + verify into staging
    python tools/mirror_models.py upload    # create/update the release
    python tools/mirror_models.py verify    # re-check the published release

``upload`` requires the GitHub CLI (``gh``) authenticated with push access.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO = "jvoltci/naina"
TAG = "models-v1"
REPO_ROOT = Path(__file__).resolve().parent.parent
STAGING = REPO_ROOT / "build" / "model-mirror"
HF = "https://huggingface.co/PaddlePaddle"

# (mirror filename, upstream url, sha256, bytes)
# Hashes were verified by downloading each artifact and hashing it; they are
# the same values recorded in models/registry.yaml.
ARTIFACTS: tuple[tuple[str, str, str, int], ...] = (
    (
        "ppocrv6_tiny_det.onnx",
        f"{HF}/PP-OCRv6_tiny_det_onnx/resolve/main/inference.onnx",
        "193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8",
        1780590,
    ),
    (
        "ppocrv6_tiny_rec.onnx",
        f"{HF}/PP-OCRv6_tiny_rec_onnx/resolve/main/inference.onnx",
        "9ef676d6ed3c88256a2d92c640c44f25b0c40947e111b14b8be8f594091563e6",
        4462639,
    ),
    (
        "ppocrv6_tiny_rec_charset.yml",
        f"{HF}/PP-OCRv6_tiny_rec_onnx/resolve/main/inference.yml",
        "66170210bad538e83fff3c4a3867e547d6bf20b50d64b20347c4b913f3034ea1",
        55571,
    ),
    (
        "ppocrv6_small_det.onnx",
        f"{HF}/PP-OCRv6_small_det_onnx/resolve/main/inference.onnx",
        "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e",
        9880512,
    ),
    (
        "ppocrv6_small_rec.onnx",
        f"{HF}/PP-OCRv6_small_rec_onnx/resolve/main/inference.onnx",
        "5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634",
        21159378,
    ),
    (
        "ppocrv6_small_rec_charset.yml",
        f"{HF}/PP-OCRv6_small_rec_onnx/resolve/main/inference.yml",
        "ab078671bb49f06228eadccd34f1bb501e157f7a047095ffb943ba81512c77d1",
        150579,
    ),
    (
        "ppocrv6_medium_det.onnx",
        f"{HF}/PP-OCRv6_medium_det_onnx/resolve/main/inference.onnx",
        "eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1",
        62032837,
    ),
    (
        "ppocrv6_medium_rec.onnx",
        f"{HF}/PP-OCRv6_medium_rec_onnx/resolve/main/inference.onnx",
        "9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba",
        76554979,
    ),
    (
        "ppocrv6_medium_rec_charset.yml",
        f"{HF}/PP-OCRv6_medium_rec_onnx/resolve/main/inference.yml",
        "991b700facf5b50a7de193468207d5f4255b538dde0d312ae3b7c7a9b6873129",
        150580,
    ),
    (
        "ppdoclayoutv3.onnx",
        f"{HF}/PP-DocLayoutV3_onnx/resolve/main/inference.onnx",
        "45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba",
        130502049,
    ),
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch() -> int:
    STAGING.mkdir(parents=True, exist_ok=True)
    failures = 0
    for name, url, want_sha, want_bytes in ARTIFACTS:
        dest = STAGING / name
        if dest.exists() and dest.stat().st_size == want_bytes:
            got = sha256_file(dest)
            if got == want_sha:
                print(f"  cached  {name}")
                continue
        print(f"  fetch   {name}  ({want_bytes / 1e6:.1f} MB)")
        tmp = dest.with_suffix(dest.suffix + ".part")
        urllib.request.urlretrieve(url, tmp)  # noqa: S310 - fixed https URLs
        got_bytes = tmp.stat().st_size
        got = sha256_file(tmp)
        if got != want_sha or got_bytes != want_bytes:
            print(f"    MISMATCH {name}")
            print(f"      want {want_sha}  {want_bytes} bytes")
            print(f"      got  {got}  {got_bytes} bytes")
            tmp.unlink()
            failures += 1
            continue
        tmp.rename(dest)
    total = sum(a[3] for a in ARTIFACTS)
    print(f"\n{len(ARTIFACTS)} artifacts, {total / 1e6:.1f} MB total -> {STAGING}")
    if failures:
        print(f"{failures} artifact(s) failed verification", file=sys.stderr)
    return 1 if failures else 0


def upload() -> int:
    missing = [n for n, _, _, _ in ARTIFACTS if not (STAGING / n).exists()]
    if missing:
        print(f"missing from staging (run `fetch` first): {missing}", file=sys.stderr)
        return 1

    notes = (
        "Mirrored model weights for naina.\n\n"
        "Every artifact here is redistributed unmodified from PaddleOCR under "
        "the Apache License 2.0. See the NOTICE file in the repository root "
        "for attribution and the upstream source URL of each file.\n\n"
        "naina points at this release rather than at upstream hosting so that "
        "an upstream re-tag, move, or deletion cannot break installs. Each "
        "file is pinned by sha256 in `models/registry.yaml`, so a corrupted or "
        "substituted download fails closed.\n"
    )

    existing = subprocess.run(
        ["gh", "release", "view", TAG, "--repo", REPO],
        capture_output=True,
        text=True,
        check=False,
    )
    if existing.returncode != 0:
        print(f"creating release {TAG}")
        subprocess.run(
            [
                "gh", "release", "create", TAG,
                "--repo", REPO,
                "--title", "Model weights v1",
                "--notes", notes,
            ],
            check=True,
        )
    else:
        print(f"release {TAG} already exists; uploading with --clobber")

    files = [str(STAGING / n) for n, _, _, _ in ARTIFACTS]
    subprocess.run(
        ["gh", "release", "upload", TAG, "--repo", REPO, "--clobber", *files],
        check=True,
    )
    print("upload complete")
    return 0


def verify() -> int:
    """Download each artifact back from the release and re-hash it."""
    base = f"https://github.com/{REPO}/releases/download/{TAG}"
    tmpdir = STAGING / "verify"
    tmpdir.mkdir(parents=True, exist_ok=True)
    failures = 0
    for name, _, want_sha, want_bytes in ARTIFACTS:
        dest = tmpdir / name
        try:
            urllib.request.urlretrieve(f"{base}/{name}", dest)  # noqa: S310
        except Exception as e:  # noqa: BLE001
            print(f"  FAIL    {name}: {e}")
            failures += 1
            continue
        got = sha256_file(dest)
        ok = got == want_sha and dest.stat().st_size == want_bytes
        print(f"  {'ok' if ok else 'MISMATCH':8} {name}")
        if not ok:
            failures += 1
        dest.unlink()
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("command", choices=("fetch", "upload", "verify"))
    args = ap.parse_args()
    return {"fetch": fetch, "upload": upload, "verify": verify}[args.command]()


if __name__ == "__main__":
    raise SystemExit(main())
