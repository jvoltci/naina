<p align="center">
  <img src="https://raw.githubusercontent.com/jvoltci/naina/master/bindings/flutter/docs/assets/hero.svg" alt="naina" width="620">
</p>

<p align="center">
  <a href="https://pub.dev/packages/naina"><img src="https://img.shields.io/pub/v/naina.svg" alt="pub"></a>
  <a href="https://github.com/jvoltci/naina/blob/master/LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="license"></a>
  <img src="https://img.shields.io/badge/platforms-Android%20%7C%20iOS-lightgrey.svg" alt="platforms">
</p>

<h3 align="center">Read text from images, on the device.</h3>

<p align="center">
  <a href="https://jvoltci.github.io/naina/doc/">Documentation</a> ·
  <a href="https://jvoltci.github.io/naina/">Try it in a browser</a> ·
  <a href="https://github.com/jvoltci/naina">Source</a>
</p>

```dart
final naina = await Naina.open();

final page = naina.readRgbSync(rgb, width: w, height: h);
print(page.text);

naina.close();
```

Runs entirely on the device over FFI to naina's C++ core — no network calls, no
API key. The same core and the same PP-OCRv6 models as naina's Python and Node
packages.

## Install

```yaml
dependencies:
  naina: ^0.2.0
```

Model weights (about 11 MB for the default tier) download once on first use and
are cached. On mobile, pass a writable directory:

```dart
import 'package:path_provider/path_provider.dart';

final dir = await getApplicationSupportDirectory();
final naina = await Naina.open(modelsRoot: '${dir.path}/naina');
```

Required on iOS and Android: the app sandbox cannot write to the default
`~/.cache` location.

## Getting RGB bytes

naina ships no image decoder — Flutter already has one.

```dart
import 'dart:ui' as ui;

Future<(Uint8List, int, int)> decodeToRgb(Uint8List fileBytes) async {
  final codec = await ui.instantiateImageCodec(fileBytes);
  final frame = await codec.getNextFrame();
  final image = frame.image;

  final rgba = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
  final src = rgba!.buffer.asUint8List();

  final rgb = Uint8List(image.width * image.height * 3);
  for (var i = 0, j = 0; i < src.length; i += 4, j += 3) {
    rgb[j] = src[i];
    rgb[j + 1] = src[i + 1];
    rgb[j + 2] = src[i + 2];
  }
  return (rgb, image.width, image.height);
}
```

Decode at native resolution and let naina resize; its resize is the same code on
every platform, so results stay consistent across devices.

## Keep it off the UI thread

A full page takes noticeable time. `readRgbSync` runs on the calling isolate, so
use a background isolate for anything user-facing:

```dart
final page = await readRgbInIsolate(rgb, width: w, height: h);
```

That helper opens its own context and so reloads the models on each call. For
repeated reads, keep one long-lived isolate.

## Tiers

| Tier | Weights | Use for |
|---|---|---|
| `NainaTier.tiny` *(default)* | ~11 MB | phones |
| `NainaTier.small` | ~54 MB | tablets, or when accuracy matters more than size |

A tier picks model size, not capability.

## Limitations

**Unsupported scripts return confident nonsense.** The character set covers Latin
and CJK; there is no Devanagari. A Devanagari page comes back as plausible Latin
at around 0.75 confidence, because `confidence` measures certainty within the
model's own alphabet and cannot express "not in my alphabet". Check the expected
language before calling naina rather than relying on confidence.

**Handwriting is weak.** PP-OCRv6 is trained on print.

**Text only.** Layout structure and markdown exist in the core and are exposed by
naina's Python, Node and browser packages, not yet here.

**`arm64-v8a` and `x86_64` only.** No 32-bit ABIs.

Full list: [jvoltci.github.io/naina/doc/limits](https://jvoltci.github.io/naina/doc/limits/).

## Status

Verified: Dart analysis clean, every C symbol resolves, FFI struct layout matches
the C compiler's (`naina_config` is 40 bytes on both sides), and image wrapping,
context lifecycle and ABI v1 compatibility all exercised against a real
`libnaina`.

Not yet verified: the Android NDK build and the iOS podspec have not been run on a
device. Treat on-device use as a preview until they have.

## The name

*naina* means **eyes** in Hindi.
