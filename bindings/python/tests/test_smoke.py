"""Smoke test: confirm naina imports, an Engine can be created (or fails
cleanly with NAINA_E_BACKEND_UNAVAIL in a core-only build), and that the
public OCR surface has the expected shape."""

from __future__ import annotations

import os

import numpy as np
import pytest

import naina


def test_version_attr():
    assert isinstance(naina.__version__, str)
    assert "." in naina.__version__


def test_public_surface_is_ocr_shaped():
    for name in ("Engine", "Page", "Line", "Tier", "Backend", "read"):
        assert hasattr(naina, name), f"missing {name}"
    # The face API is gone; it lives on the face-stack branch.
    for gone in ("detect_faces", "embed_face", "similarity", "Face"):
        assert not hasattr(naina, gone), f"{gone} should not be exported"


def test_tier_enum_values():
    # pybind11 enums are not iterable; __members__ is the portable accessor.
    assert set(naina.Tier.__members__) == {"AUTO", "TINY", "SMALL", "MEDIUM"}


def _find_scalable_font(size: int):
    """Locate any scalable TrueType font, on any platform.

    The bitmap fallback in Pillow renders text far too small for a text
    detector to find, so a real font is required — but hardcoding one path
    makes the test macOS-only. Returns None if nothing usable is found, so the
    caller can skip rather than fail.
    """
    from PIL import ImageFont

    candidates = [
        # macOS
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        # Debian / Ubuntu
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        # Fedora / RHEL / Arch
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        # Alpine
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        # Windows
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    # Last resort: let Pillow search its own configured font directories.
    for name in ("DejaVuSans.ttf", "LiberationSans-Regular.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return None


def _engine_or_skip(**kwargs):
    try:
        return naina.Engine(**kwargs)
    except naina.NainaError as e:
        pytest.skip(f"no inference backend available: {e}")


def test_engine_constructs_and_rejects_bad_arrays(tmp_path):
    os.environ["NAINA_OFFLINE"] = "1"
    os.environ["NAINA_CACHE"] = str(tmp_path / "naina-cache")
    engine = _engine_or_skip()

    # Wrong channel count is a caller error, not an inference failure.
    with pytest.raises(ValueError):
        engine.read(np.zeros((16, 16, 4), dtype=np.uint8))


def test_read_without_weights_raises(tmp_path):
    """With an empty cache and NAINA_OFFLINE=1, read must raise rather than
    return an empty page — a silent empty result would look like a blank
    document instead of a missing model."""
    os.environ["NAINA_OFFLINE"] = "1"
    os.environ["NAINA_CACHE"] = str(tmp_path / "empty-cache")
    engine = _engine_or_skip()

    img = np.full((128, 128, 3), 128, dtype=np.uint8)
    with pytest.raises(naina.NainaError):
        engine.read(img)


@pytest.mark.skipif(
    os.environ.get("NAINA_E2E") != "1",
    reason="set NAINA_E2E=1 to run against real downloaded weights",
)
def test_reads_real_text():
    """End-to-end against real PP-OCRv6 weights. Renders the fixture with
    Pillow so the test does not depend on a committed binary blob."""
    pytest.importorskip("PIL")
    from PIL import Image, ImageDraw, ImageFont

    os.environ.pop("NAINA_OFFLINE", None)
    img = Image.new("RGB", (480, 100), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    font = _find_scalable_font(40)
    if font is None:
        pytest.skip("no scalable font found on this platform to render a fixture")
    draw.text((24, 24), "HELLO WORLD", fill=(0, 0, 0), font=font)

    engine = _engine_or_skip(tier=naina.Tier.TINY)
    page = engine.read(np.asarray(img))

    assert len(page) >= 1
    assert "HELLO" in page.markdown.upper()
    assert "WORLD" in page.markdown.upper()
    # Page surface behaves as documented.
    assert page.text == page.markdown
    assert str(page) == page.markdown
    assert page.json.startswith("{") and page.json.endswith("}")
    line = page.lines[0]
    assert line.confidence > 0.3
    assert len(line.quad) == 4
    assert isinstance(line.text, str)
