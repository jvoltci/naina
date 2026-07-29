# Changelog

## 0.2.0

First release of the Flutter binding.

- `Naina.open()` / `readRgbSync()` / `close()` over FFI to naina's C ABI
- `readRgbInIsolate()` helper for keeping reads off the UI thread
- `tiny` (~11 MB) and `small` (~54 MB) tiers
- Android: NDK build of the C++ core, ONNX Runtime from the official AAR,
  `arm64-v8a` and `x86_64`
- iOS: podspec against `onnxruntime-c`

Verified: Dart analysis clean; FFI struct layout confirmed to match the C
compiler's (`naina_config` is 40 bytes on both sides); every C symbol resolves;
image wrapping, context lifecycle and ABI v1 compatibility exercised against a
real `libnaina`.

Not yet verified: the Android NDK build and the iOS podspec have not been run on
a device. Treat on-device use as a preview until they have.

Known limits carried from the core: no Devanagari (and unsupported scripts return
confident nonsense rather than an error), weak handwriting, text only — no layout
or markdown in this binding yet.
