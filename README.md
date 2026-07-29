<p align="center">
  <img src="docs/assets/hero.svg" alt="naina hero banner" width="100%">
</p>

<p align="center">
  <a href="https://github.com/jvoltci/naina/actions/workflows/ci.yml"><img src="https://github.com/jvoltci/naina/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/jvoltci/naina/blob/master/LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="License"></a>
  <a href="https://github.com/jvoltci/naina/stargazers"><img src="https://img.shields.io/github/stars/jvoltci/naina.svg?style=social" alt="GitHub stars"></a>
</p>

<p align="center">
  <a href="https://pypi.org/project/naina/"><img src="https://img.shields.io/pypi/v/naina?label=pypi&color=3775A9" alt="PyPI"></a>
  <a href="https://www.npmjs.com/package/@jvoltci/naina"><img src="https://img.shields.io/npm/v/@jvoltci/naina?label=npm&color=CB3837" alt="npm"></a>
  <a href="https://www.npmjs.com/package/@jvoltci/naina-wasm"><img src="https://img.shields.io/npm/v/@jvoltci/naina-wasm?label=npm%20wasm&color=654FF0" alt="npm wasm"></a>
  <a href="https://pub.dev/packages/naina"><img src="https://img.shields.io/pub/v/naina?label=pub.dev&color=0175C2" alt="pub.dev"></a>
  <a href="https://crates.io/crates/naina"><img src="https://img.shields.io/crates/v/naina?label=crates.io&color=E43717" alt="crates.io"></a>
</p>

<h3 align="center">Read any document. One C++ core, everywhere.</h3>

<p align="center">
  <a href="https://jvoltci.github.io/naina/"><b>Try it online</b></a> ·
  <a href="https://jvoltci.github.io/naina/doc/"><b>Documentation</b></a> ·
  <a href="docs/ARCHITECTURE.md"><b>Architecture</b></a> ·
  <a href="docs/ROADMAP.md"><b>Roadmap</b></a> ·
  <a href="https://github.com/jvoltci/naina/releases/tag/models-v1"><b>Model weights</b></a>
</p>

**[Use it in your browser now →](https://jvoltci.github.io/naina/)** No install, no
upload, no account. PDFs and images, in ten scripts.

## Scripts

| `language` | Reads |
|---|---|
| *(default)* | Latin, Chinese, Japanese |
| `arabic` | Arabic, Persian, Urdu |
| `cyrillic` | Russian, Bulgarian, Serbian, Mongolian |
| `devanagari` | Hindi, Marathi, Nepali, Sanskrit |
| `el` | Greek |
| `eslav` | Ukrainian, Belarusian, Russian |
| `korean` | Korean |
| `ta` | Tamil |
| `te` | Telugu |
| `th` | Thai |

```python
page = naina.read("invoice.png", language="devanagari")
```

A tier picks model *size*; a language picks the *alphabet*. Detection and layout
are script-agnostic and shared, so a language costs one 8 MB model rather than
three.

> **Choosing wrong is silent.** Read a Hindi page with the default alphabet and it
> returns fluent-looking Latin at ~0.75 confidence, not an error — confidence
> measures certainty *within* the model's own alphabet and cannot express "wrong
> alphabet". An unrecognised `language` value **does** raise.

> **v0.2.0 is out on PyPI and crates.io.** The web app and docs are live. npm and
> pub.dev are blocked on account setup rather than on code — see the table below.

## Packages

| Package | Registry | What it does | State |
|---|---|---|---|
| `naina` | [PyPI](https://pypi.org/project/naina/) | Python, self-contained wheels | ✅ **published** |
| `naina` | [crates.io](https://crates.io/crates/naina) | Rust over the C ABI | ✅ **published** |
| `@jvoltci/naina` | npm | Node, inference off the event loop | built; publish blocked, see below |
| `@jvoltci/naina-wasm` | npm | Browser, 143 KB brotli | built; publish blocked |
| `naina` | pub.dev | Flutter, FFI | built; Android verified on a device, iOS unproven |
| — | — | [MCP server](mcp/) for LLM tools | works, in-repo |

The npm publish fails with `E404` on `PUT @jvoltci/naina`, which means the
`@jvoltci` scope is not resolvable for the authenticated account — it must exist
as an npm **org** or match the account's username. (Unscoped `naina` on npm is
already taken by someone else, so the scope is not optional.)

```python
import naina
import numpy as np
from PIL import Image

img = np.asarray(Image.open("invoice.png").convert("RGB"))

# One-liner: image -> markdown
print(naina.read(img))

# Or keep an Engine around
engine = naina.Engine(tier=naina.Tier.SMALL)
page = engine.read(img)
for line in page.lines:
    print(f"{line.confidence:.3f}  {line.text}")
```

## Why

OCR accuracy is a solved commodity. PP-OCRv6's weights are Apache-2.0, so naina
runs the same models PaddleOCR runs and gets the same accuracy. Competing there
is unwinnable and pointless.

The gap is **distribution**. Every existing tool is locked into one lane:

| Tool | Lane | Cannot do |
| --- | --- | --- |
| PaddleOCR | Python, server | No Node/Rust/browser/C ABI. Training framework first, huge surface |
| RapidOCR | Multi-language, as *separate ports* | Behaviour drifts between the Python, C++, Java and .NET versions |
| oar-ocr | Rust only | No Python/Node bindings, no shipped WASM |
| retto | Rust only, det+rec only | No bindings, no layout |
| client-ocr | Browser only | No server, no native |
| ML Kit (Google Lens) | Mobile only, closed weights | Cannot self-host; 5 scripts only |
| MinerU / marker / docling | Python, GPU-leaning | Licence traps, heavy installs |

Nobody ships one engine that runs identically everywhere. This is llama.cpp's
playbook applied to OCR: llama.cpp won on portability and zero dependencies, not
on inference math.

**No OpenCV. No pyclipper. No PaddlePaddle.** The convex hull, minimum-area
rectangle, polygon offset and contour tracing are ~450 lines of tested C++,
because a 300 MB dependency tree would defeat the point of an 11 MB tier.

## What you get

**Three device tiers.** A size axis, not a licence axis — every model naina ships
is Apache-2.0 and safe for commercial use.

| Tier | det | rec | layout | Total | Target | Charset |
| --- | --- | --- | --- | --- | --- | --- |
| `tiny` | 1.8 MB | 4.5 MB | 4.9 MB | **≈ 11 MB** | Browser, phone, Pi Zero | 6,904 (CJK + Latin) |
| `small` | 9.9 MB | 21.2 MB | 23.5 MB | **≈ 55 MB** | Laptop, Pi 5, mobile app | 18,708 (50 languages) |
| `medium` | 62.0 MB | 76.6 MB | 130.5 MB | **≈ 269 MB** | Server, desktop | 18,708 (50 languages) |

PaddleOCR ships no ONNX build of the small layout models, so naina converts them
itself — byte-deterministically, and verified per-column against the Paddle
original. Without that, layout would exist only at the 269 MB tier and an 11 MB
browser build could not describe document structure. See
[`tools/paddle2onnx_layout.py`](tools/paddle2onnx_layout.py).

**Every binding over one C ABI**, so behaviour cannot drift between languages:

| Binding | Status | Install |
| --- | --- | --- |
| C / C++ | ✅ works | `naina.h` — the contract every other binding targets |
| Python | ✅ works, unpublished | build from source; `pip install naina` once released |
| Node / TypeScript | ✅ works, unpublished | build from source; needs a local toolchain |
| Rust | ❌ v0.5 | — |
| WASM / browser | ❌ v0.4 | — |

**Weights are mirrored, not borrowed.** naina fetches from
[its own release](https://github.com/jvoltci/naina/releases/tag/models-v1), not
from upstream hosting, so an upstream re-tag or deletion cannot break installs.
Every file is pinned by sha256, so a corrupted or substituted download fails
closed rather than producing silently wrong output. Provenance for each artifact
is recorded in [`NOTICE`](NOTICE) and as a `source_url` in the manifest.

## Benchmarks

Real measurements, not vendor claims.

**End-to-end, `tiny` tier, Apple M3 Pro** — rendered text fixture, 480×140:

| Line | Recognised | Recognition conf | Detection score |
| --- | --- | --- | --- |
| 1 | `HELLO WORLD` | 0.967 | 0.899 |
| 2 | `naina 2026` | 1.000 | 0.925 |

Reproduce: `ctest --preset macos-arm64 -R test_ocr_e2e --output-on-failure`

> **On the accuracy numbers everyone quotes.** Vendors self-report 96.33% on
> OmniDocBench v1.6 while independent evaluation of the same benchmark tops out
> around 90.1%. naina will publish per-device numbers with the harness in-repo
> and the command to reproduce them, or publish nothing. A full benchmark matrix
> lands with v1.0.

## Status

**v0.2 — text spotting works end to end.** The honest state:

| Component | Status |
| --- | --- |
| C ABI (`naina_read`, page accessors, stage-level access) | ✅ |
| Model registry — manifest-driven, sha256-verified, tier fallback | ✅ |
| Detection — PP-OCRv6 det, DBNet decode | ✅ |
| Recognition — PP-OCRv6 rec, CTC greedy decode | ✅ |
| Geometry — convex hull, min-area rect, polygon offset, no OpenCV | ✅ |
| Page storage — pointer-stable, markdown + JSON | ✅ |
| Python binding | ✅ |
| Node binding | ✅ |
| ONNX Runtime backend | ✅ |
| NCNN backend | ⚠️ compiles, but `FindNCNN.cmake` does not locate a brew install |
| Recognition batching (one strip per call today) | ⚠️ correct but unoptimised |
| Layout analysis → structured markdown | ❌ v0.3 |
| WASM + browser app | ❌ v0.4 |
| Rust binding | ❌ v0.5 |
| Cross-binding parity enforced in CI | ❌ v1.0 |
| MCP server (`mcp/`, 2 tools, verified over stdio) | ✅ |

13 C++ tests, 6 Python tests, 6 Node tests. CI builds on Linux (gcc + clang) and
macOS arm64.

**Not supported, deliberately:** handwriting (PP-OCRv6 is weak at it and claiming
otherwise would be dishonest), autoregressive VLM parsing, training, and
chart/formula semantic extraction.

## Install

Not yet on PyPI or npm — see the note at the top. Build from source:

```bash
cmake --preset macos-arm64           # or linux-x86_64, linux-arm64, windows-x86_64
cmake --build --preset macos-arm64
ctest --preset macos-arm64
```

Requires CMake ≥ 3.24, a C++20 compiler, `yaml-cpp`, `libcurl`, and ONNX Runtime.

naina ships **no image decoder** on purpose — it takes raw pixels. Use Pillow,
OpenCV, `sharp`, or anything else that hands you a buffer.

## Environment

| Variable | Effect |
| --- | --- |
| `NAINA_CACHE` | Where weights are cached. Default `~/.cache/naina/models` |
| `NAINA_OFFLINE=1` | Disable network; use only what is already cached |
| `NAINA_REGISTRY` | Path to `registry.yaml`. Both bindings set this automatically |

## MCP server

An agent can read documents through naina directly:

```json
{
  "mcpServers": {
    "naina": { "command": "npx", "args": ["-y", "@jvoltci/naina-mcp"] }
  }
}
```

Two tools: `read_document` (markdown) and `read_document_detailed` (per-line
text, confidence, quads). See [`mcp/README.md`](mcp/README.md).

Reading a page carries no session state, so the server is written stateless —
which is what MCP spec revision 2026-07-28 formalised. Note that the current
SDK (`1.30.0`) only negotiates up to `2025-11-25`; the newer revision is a
dependency bump away, not a rewrite.

## Documentation

- [**Architecture**](docs/ARCHITECTURE.md) — the C ABI, backends, model registry
- [**Roadmap**](docs/ROADMAP.md) — what ships when
- [**Design spec**](docs/superpowers/specs/2026-07-28-naina-ocr-design.md) — why naina is shaped this way
- [**Contributing**](CONTRIBUTING.md)

## The name

*naina* (नैना) means **eyes** in Hindi. The library reads.

It began as a face-recognition runtime under the same name. That work is
preserved on the [`face-stack`](https://github.com/jvoltci/naina/tree/face-stack)
branch, and the engine it produced — C ABI, backend abstraction, manifest-driven
model loader — is what made this pivot cheap.

## Contributing

PRs welcome. See [CONTRIBUTING.md](CONTRIBUTING.md). Open a Discussion for
anything beyond a small fix.

## License

Apache-2.0. Redistributed model weights are also Apache-2.0 — see [`NOTICE`](NOTICE).
