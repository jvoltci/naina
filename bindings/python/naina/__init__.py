"""naina — embeddable document reading runtime.

Quickstart:

    import naina
    import numpy as np
    from PIL import Image

    img = np.asarray(Image.open("invoice.png").convert("RGB"))

    # One-liner: image -> markdown
    print(naina.read(img))

    # Or keep an Engine around for repeated calls
    engine = naina.Engine(tier=naina.Tier.SMALL)
    page = engine.read(img)
    print(page.markdown)
    for line in page.lines:
        print(f"{line.confidence:.3f}  {line.text}")

naina ships no image decoder on purpose — it takes raw pixels as a
``(H, W, 3)`` uint8 RGB array, or ``(H, W)`` grayscale. Use Pillow, OpenCV, or
anything else that produces a numpy array.

Tiers select model size, not licence. Every model naina ships is Apache-2.0:

    TINY    ~11 MB   browser, phone, Pi Zero. Reduced charset (CJK + Latin).
    SMALL   ~54 MB   laptop, Pi 5, mobile app. Full 50-language charset.
    MEDIUM  ~269 MB  server, desktop. Full charset, highest accuracy.

Weights are fetched on first use and cached under $NAINA_CACHE (default
``~/.cache/naina/models``). Every download is verified against the sha256 in
the manifest, so a truncated or substituted file fails closed rather than
producing silently wrong output. Set ``NAINA_OFFLINE=1`` to disable network
access and use only what is already cached.
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
    Backend,
    Engine,
    Line,
    NainaError,
    Page,
    Point,
    Tier,
    __version__,
    read,
)

__all__ = [
    "Backend",
    "Engine",
    "Line",
    "NainaError",
    "Page",
    "Point",
    "Tier",
    "__version__",
    "read",
]
