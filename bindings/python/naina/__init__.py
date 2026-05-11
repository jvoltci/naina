"""naina — embeddable face & person CV runtime.

Quickstart:

    import naina
    import cv2

    img = cv2.imread("face.jpg")[:, :, ::-1]   # BGR -> RGB
    engine = naina.Engine()
    faces = engine.detect_faces(img)
    if faces:
        emb = engine.embed_face(img, faces[0])
        # Compare to another embedding:
        sim = naina.similarity(emb, other_emb)

Models are fetched from GitHub Releases the first time each task is used,
and cached under $NAINA_CACHE (default ~/.cache/naina/models). Set
NAINA_OFFLINE=1 to disable network and only use the local cache.
"""

from __future__ import annotations

import os as _os
from pathlib import Path as _Path

# Locate the bundled registry.yaml so NAINA_REGISTRY is set automatically
# unless the user already configured one.
_pkg_dir = _Path(__file__).resolve().parent
_bundled_registry = _pkg_dir / "models" / "registry.yaml"
if "NAINA_REGISTRY" not in _os.environ and _bundled_registry.exists():
    _os.environ["NAINA_REGISTRY"] = str(_bundled_registry)

from ._binding import (  # noqa: E402
    BBox,
    Backend,
    Engine,
    Face,
    NainaError,
    Point,
    __version__,
    similarity,
)

__all__ = [
    "BBox",
    "Backend",
    "Engine",
    "Face",
    "NainaError",
    "Point",
    "__version__",
    "similarity",
]
