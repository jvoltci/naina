## 0.2.1

- README hero renders on pub.dev. GitHub raw serves SVG as text/plain with
  nosniff, so browsers refuse to render it as an image; the hero is now a PNG.
- Renamed the package doc directory to doc/ per the pub layout convention, and
  added LICENSE to the package root, both required by pub.dev.
- Eight further scripts: arabic, cyrillic, el, eslav, korean, ta, te, th.
  Use NainaLanguage constants; NainaLanguage.all lists them.

# Changelog

## 0.2.0

First release of the Flutter binding.

- `Naina.open()` / `readRgbSync()` / `close()` over FFI to naina's C ABI
- `readRgbInIsolate()` helper for keeping reads off the UI thread
- `tiny` (~11 MB) and `small` (~54 MB) tiers
- Android: NDK build of the C++ core, ONNX Runtime from the official AAR,
  `arm64-v8a` and `x86_64`
- iOS: podspec against `onnxruntime-c`

- `NainaModels.stage()` fetches weights on the device. The Android core has no
  libcurl, so Dart downloads; the C core still decides the paths and verifies the
  hashes.
- `language:` selects the recognition alphabet — `'devanagari'` reads Hindi,
  Marathi, Nepali and Sanskrit. An unknown value throws.
- Requires Android API 28+ (`std::aligned_alloc`).

Verified on an arm64 Android emulator: weights staged, then a real A4 page read as
33 lines at 0.992 mean confidence, matching the native build. Three on-device
integration tests pass, plus 11 host FFI tests including a struct-layout check
against the C compiler.

**iOS is not verified**: the podspec is written but has never been built or run.

Known limits from the core: weak handwriting; Arabic, Tamil, Telugu, Thai, Korean
and Cyrillic unsupported (and wrong-alphabet reads return plausible wrong text
rather than an error); text only — no layout or markdown in this binding yet.
