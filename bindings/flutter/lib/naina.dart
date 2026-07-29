/// On-device OCR for Flutter.
///
/// Reads text out of an image using naina's C++ core over FFI. Nothing leaves
/// the device: there is no network call and no cloud API.
///
/// ```dart
/// final naina = await Naina.open();               // Latin + CJK
/// // final naina = await Naina.open(language: 'devanagari');   // Hindi
/// final page = naina.readRgbSync(rgb, width: w, height: h);
/// print(page.text);
/// naina.close();
/// ```
///
/// Scope is deliberately narrow — text extraction. The C++ core also produces
/// layout structure and markdown; those are exposed by naina's Python, Node and
/// browser packages and may arrive here later.
library;

import 'dart:async';
import 'dart:ffi';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'src/bindings.dart';

export 'src/bindings.dart'
    show NainaException, NainaLanguage, NainaStatus, NainaTier;
export 'src/models.dart' show ModelFile, NainaModels, StagingProgress;

/// One recognised line of text.
class NainaLine {
  const NainaLine({
    required this.text,
    required this.confidence,
    required this.corners,
  });

  final String text;

  /// Mean per-character probability over the characters actually emitted.
  ///
  /// Beware: this cannot express "this script is not in my alphabet". Read a
  /// Devanagari page with the default Latin alphabet and it still returns
  /// plausible-looking Latin at around 0.75. Pass `language: 'devanagari'` for
  /// Hindi rather than trusting confidence to warn you.
  final double confidence;

  /// Four corners, clockwise from top-left, in source-image pixels. Not
  /// necessarily axis-aligned — rotated text gives a rotated quad.
  final List<Offset2D> corners;

  @override
  String toString() => '$text (${confidence.toStringAsFixed(2)})';
}

/// A bare (x, y), so this package needs no Flutter widget imports.
class Offset2D {
  const Offset2D(this.x, this.y);
  final double x;
  final double y;

  @override
  String toString() => '($x, $y)';
}

/// The result of reading one image.
class NainaPageResult {
  const NainaPageResult({required this.lines});

  final List<NainaLine> lines;

  /// Every line joined by newlines, in reading order.
  String get text => lines.map((l) => l.text).join('\n');

  /// Mean confidence across all lines, or 0 when nothing was found.
  double get meanConfidence => lines.isEmpty
      ? 0
      : lines.map((l) => l.confidence).reduce((a, b) => a + b) / lines.length;
}

/// An open naina context.
///
/// Holds loaded models — tens to hundreds of megabytes — so call [close] when
/// finished. Garbage collection will not release it.
class Naina {
  Naina._(this._bindings, this._ctx);

  final NainaBindings _bindings;
  Pointer<NainaCtx> _ctx;

  /// Open a context.
  ///
  /// [tier] selects model size, not capability: [NainaTier.tiny] is ~11 MB and
  /// the right default on a phone; [NainaTier.small] is ~54 MB and more accurate.
  ///
  /// [language] selects the recognition alphabet: null or empty reads Latin and
  /// CJK, `'devanagari'` reads Hindi, Marathi, Nepali and Sanskrit. An unknown
  /// value throws — reading Devanagari with the Latin model returns
  /// plausible-looking wrong text rather than an error, which is exactly what
  /// this prevents.
  ///
  /// [modelsRoot] is where weights are cached. On mobile, pass a path inside the
  /// app's own documents directory — the default (`~/.cache`) is not writable in
  /// a sandboxed app.
  static Future<Naina> open({
    int tier = NainaTier.tiny,
    String? modelsRoot,
    int numThreads = 0,
    String? language,
  }) async {
    final bindings = NainaBindings(openNainaLibrary());

    final cfg = calloc<NainaConfig>();
    final ctxOut = calloc<Pointer<NainaCtx>>();
    Pointer<Utf8> rootPtr = nullptr;
    Pointer<Utf8> langPtr = nullptr;
    try {
      if (modelsRoot != null) {
        rootPtr = modelsRoot.toNativeUtf8();
      }
      if (language != null && language.isNotEmpty) {
        langPtr = language.toNativeUtf8();
      }
      cfg.ref
        ..version = 3
        ..backend = 0 // NAINA_BACKEND_AUTO
        ..device = 0 // NAINA_DEVICE_AUTO
        ..modelsRoot = rootPtr
        ..numThreads = numThreads
        ..enableResearchModels = 0
        ..tier = tier
        ..language = langPtr;

      final rc = bindings.init(cfg, ctxOut);
      if (rc != NainaStatus.ok || ctxOut.value == nullptr) {
        throw NainaException(rc, bindings.statusStr(rc).toDartString());
      }
      return Naina._(bindings, ctxOut.value);
    } finally {
      if (rootPtr != nullptr) calloc.free(rootPtr);
      if (langPtr != nullptr) calloc.free(langPtr);
      calloc.free(cfg);
      calloc.free(ctxOut);
    }
  }

  /// naina's version string.
  String get version => _bindings.versionString().toDartString();

  /// Read packed RGB8 bytes.
  ///
  /// [rgb] must be exactly `width * height * 3` long. Decoding an image file to
  /// RGB is the caller's job — naina ships no image decoder, and Flutter already
  /// has one (see the README for the `dart:ui` recipe).
  ///
  /// Runs on the calling isolate. A full page takes noticeable time, so call
  /// this from a background isolate if it would block a frame; see [readRgbInIsolate].
  NainaPageResult readRgbSync(
    Uint8List rgb, {
    required int width,
    required int height,
  }) {
    if (_ctx == nullptr) {
      throw const NainaException(NainaStatus.notInitialized, 'context is closed');
    }
    final expected = width * height * 3;
    if (width <= 0 || height <= 0 || rgb.length < expected) {
      throw NainaException(
        NainaStatus.invalidArg,
        'expected $expected bytes for ${width}x$height, got ${rgb.length}',
      );
    }

    // Copy into native memory. Dart's GC can move a typed-data backing store,
    // and naina_image_wrap keeps a VIEW rather than copying, so a moved buffer
    // would be read after relocation.
    final pixels = calloc<Uint8>(expected);
    final imgOut = calloc<Pointer<NainaImage>>();
    final pageOut = calloc<Pointer<NainaPage>>();
    Pointer<NainaImage> img = nullptr;
    Pointer<NainaPage> page = nullptr;

    try {
      pixels.asTypedList(expected).setAll(0, rgb.sublist(0, expected));

      var rc = _bindings.imageWrap(
          pixels, width, height, width * 3, NainaPixfmt.rgb8, imgOut);
      if (rc != NainaStatus.ok) {
        throw NainaException(rc, _bindings.statusStr(rc).toDartString());
      }
      img = imgOut.value;

      rc = _bindings.read(_ctx, img, pageOut);
      if (rc != NainaStatus.ok || pageOut.value == nullptr) {
        throw NainaException(rc, _bindings.statusStr(rc).toDartString());
      }
      page = pageOut.value;

      return _collect(page);
    } finally {
      if (page != nullptr) _bindings.pageRelease(page);
      if (img != nullptr) _bindings.imageRelease(img);
      calloc.free(pageOut);
      calloc.free(imgOut);
      calloc.free(pixels);
    }
  }

  /// Copy everything out of the page before it is released.
  ///
  /// The page owns its strings, so each Dart String must be materialised here —
  /// holding the pointers past pageRelease would read freed memory.
  NainaPageResult _collect(Pointer<NainaPage> page) {
    final linesOut = calloc<Pointer<NainaTextline>>();
    final countOut = calloc<Int32>();
    try {
      final rc = _bindings.pageLines(page, linesOut, countOut);
      if (rc != NainaStatus.ok) {
        throw NainaException(rc, _bindings.statusStr(rc).toDartString());
      }
      final count = countOut.value;
      final base = linesOut.value;

      final lines = <NainaLine>[];
      for (var i = 0; i < count; i++) {
        final l = (base + i).ref;
        final corners = <Offset2D>[
          for (var c = 0; c < 4; c++)
            Offset2D(l.box.corners[c].x, l.box.corners[c].y),
        ];
        lines.add(NainaLine(
          text: l.text == nullptr ? '' : l.text.toDartString(),
          confidence: l.confidence,
          corners: corners,
        ));
      }
      return NainaPageResult(lines: lines);
    } finally {
      calloc.free(countOut);
      calloc.free(linesOut);
    }
  }

  /// Release the native context. Further reads throw.
  void close() {
    if (_ctx != nullptr) {
      _bindings.release(_ctx);
      _ctx = nullptr;
    }
  }
}

/// Read an image on a background isolate, so the UI thread keeps painting.
///
/// Opens its own context inside the isolate, because a `Pointer` cannot cross an
/// isolate boundary. That means it also reloads the models, so for repeated
/// reads keep one long-lived isolate rather than calling this in a loop.
Future<NainaPageResult> readRgbInIsolate(
  Uint8List rgb, {
  required int width,
  required int height,
  int tier = NainaTier.tiny,
  String? modelsRoot,
  String? language,
}) {
  return Isolate.run(() async {
    final naina =
        await Naina.open(tier: tier, modelsRoot: modelsRoot, language: language);
    try {
      return naina.readRgbSync(rgb, width: width, height: height);
    } finally {
      naina.close();
    }
  });
}
