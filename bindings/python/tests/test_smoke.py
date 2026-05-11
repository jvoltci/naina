"""Smoke test: confirm naina imports, an Engine can be created (or fails
cleanly with NAINA_E_BACKEND_UNAVAIL in a core-only build), and basic
shape/typing of the public surface."""

from __future__ import annotations

import os

import numpy as np
import pytest

import naina


def test_version_attr():
    assert isinstance(naina.__version__, str)
    assert "." in naina.__version__


def test_similarity_identity():
    v = np.array([0.5, 0.5, 0.5, 0.5], dtype=np.float32)
    assert naina.similarity(v, v) == pytest.approx(1.0, abs=1e-5)


def test_similarity_orthogonal():
    a = np.array([1.0, 0.0], dtype=np.float32)
    b = np.array([0.0, 1.0], dtype=np.float32)
    assert naina.similarity(a, b) == pytest.approx(0.0, abs=1e-6)


def test_engine_lifecycle_offline():
    """Engine should construct cleanly. detect_faces will fail with no
    weights on disk (offline mode); we just verify the API contract."""
    os.environ["NAINA_OFFLINE"] = "1"
    try:
        engine = naina.Engine()
    except naina.NainaError as e:
        # No backend compiled in: acceptable for the core-only matrix.
        pytest.skip(f"No backend available: {e}")

    img = np.full((128, 128, 3), 128, dtype=np.uint8)
    with pytest.raises(naina.NainaError):
        engine.detect_faces(img)  # weights not on disk and NAINA_OFFLINE=1
