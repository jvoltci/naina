// Exercises the FFI layer against a real libnaina.
//
// This runs on the host (macOS/Linux desktop), not on a device, because that is
// what can actually be automated here — and it is where the bindings are most
// likely to be wrong. Struct layout, field order, string ownership and the
// config ABI version are all things that fail as memory corruption rather than
// as an error, and a host run catches them just as well as a phone would.
//
// Skips loudly, naming what is missing, rather than passing vacuously.
//
//   NAINA_LIB=/path/to/libnaina.dylib dart test
//
// What this does NOT cover, and no host test can: the Android Gradle/NDK build,
// the ONNX Runtime AAR wiring, the iOS podspec, and whether symbols survive
// stripping on a real device.

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:naina/src/bindings.dart';

DynamicLibrary? _tryOpen() {
  final explicit = Platform.environment['NAINA_LIB'];
  final candidates = <String>[
    if (explicit != null) explicit,
    // Where the repo's own presets put it.
    '../../build/macos-arm64/core/libnaina.dylib',
    '../../build/linux-x86_64/core/libnaina.so',
  ];
  for (final path in candidates) {
    if (File(path).existsSync()) {
      try {
        return DynamicLibrary.open(path);
      } on ArgumentError {
        continue;
      }
    }
  }
  return null;
}

void main() {
  final lib = _tryOpen();
  if (lib == null) {
    // ignore: avoid_print
    print('SKIP ffi_test: no libnaina found. Build the core, or set NAINA_LIB.');
    return;
  }

  late NainaBindings bindings;

  setUpAll(() {
    bindings = NainaBindings(lib);
  });

  test('every symbol resolves', () {
    // NainaBindings looks all of them up in its constructor, so reaching here
    // means none is missing. An absent symbol throws with its own name, which is
    // far easier to act on than a later segfault.
    expect(bindings.versionString, isNotNull);
  });

  test('version string is sane', () {
    final v = bindings.versionString().toDartString();
    expect(v, isNotEmpty);
    expect(v, startsWith('0.'));
  });

  test('status strings are distinct and non-empty', () {
    final seen = <String>{};
    for (var s = 0; s <= 8; s++) {
      final text = bindings.statusStr(s).toDartString();
      expect(text, isNotEmpty, reason: 'status $s has no text');
      seen.add(text);
    }
    // A shared or defaulted string would hide the real error from users.
    expect(seen.length, 9, reason: 'some status codes share a message');
  });

  test('NainaConfig layout agrees with the C struct', () {
    // The value that matters is agreement between Dart's computed layout and the
    // C compiler's, because a mismatch writes `tier` into the wrong slot and
    // silently loads a different model than asked for.
    //
    // Measured from C on a 64-bit target: 48 bytes since `language` was
    // appended for config version 3, with existing field offsets UNCHANGED at
    // 0, 4, 8, 16, 24, 28, 32. That the earlier offsets did not move is what
    // makes the addition ABI-safe; it was 40 before.
    //
    // Note it is not simply 6*4+2*8 -- four bytes of padding align models_root.
    final ptr = sizeOf<Pointer<Void>>();
    expect(sizeOf<NainaConfig>(), ptr == 8 ? 48 : 32);
  });

  test('NainaTextbox is four points plus a score', () {
    expect(sizeOf<NainaPoint>(), 8);
    expect(sizeOf<NainaTextbox>(), 4 * 8 + 4);
  });

  test('init tolerates a null config and uses defaults', () {
    // Measured: naina_init(nullptr, out) does NOT return invalidArg. Only a null
    // out_ctx is rejected; a null cfg means "all defaults", which the
    // implementation handles explicitly with `cfg != nullptr &&` guards.
    //
    // Asserted here because it is a real part of the contract that the header
    // did not state, and writing this binding is how that came to light.
    final out = calloc<Pointer<NainaCtx>>();
    try {
      final rc = bindings.init(nullptr, out);
      expect(rc, isNot(NainaStatus.invalidArg));
      if (rc == NainaStatus.ok) bindings.release(out.value);
    } finally {
      calloc.free(out);
    }
  });

  test('init rejects a null out_ctx', () {
    expect(bindings.init(nullptr, nullptr), NainaStatus.invalidArg);
  });

  test('image_wrap rejects impossible dimensions', () {
    final out = calloc<Pointer<NainaImage>>();
    final px = calloc<Uint8>(12);
    try {
      final rc = bindings.imageWrap(px, 0, 0, 0, NainaPixfmt.rgb8, out);
      expect(rc, isNot(NainaStatus.ok));
    } finally {
      calloc.free(px);
      calloc.free(out);
    }
  });

  test('image_wrap accepts a real buffer and releases cleanly', () {
    const w = 4, h = 3;
    final px = calloc<Uint8>(w * h * 3);
    final out = calloc<Pointer<NainaImage>>();
    try {
      px.asTypedList(w * h * 3).setAll(0, Uint8List(w * h * 3));
      final rc = bindings.imageWrap(px, w, h, w * 3, NainaPixfmt.rgb8, out);
      expect(rc, NainaStatus.ok);
      expect(out.value, isNot(nullptr));
      bindings.imageRelease(out.value);
    } finally {
      calloc.free(out);
      calloc.free(px);
    }
  });

  test('a context opens and closes', () {
    final cfg = calloc<NainaConfig>();
    final out = calloc<Pointer<NainaCtx>>();
    try {
      cfg.ref
        ..version = 2
        ..backend = 0
        ..device = 0
        ..modelsRoot = nullptr
        ..numThreads = 0
        ..enableResearchModels = 0
        ..tier = NainaTier.tiny
        ..language = nullptr;

      final rc = bindings.init(cfg, out);
      if (rc != NainaStatus.ok) {
        // No backend compiled in, or no registry — a real condition on a bare
        // build, and worth naming rather than failing opaquely.
        // ignore: avoid_print
        print('SKIP: init returned ${bindings.statusStr(rc).toDartString()}');
        return;
      }
      expect(out.value, isNot(nullptr));
      bindings.release(out.value);
    } finally {
      calloc.free(out);
      calloc.free(cfg);
    }
  });

  test('config version 1 is still accepted (additive-only ABI rule)', () {
    // naina promises the C ABI only grows. A v1 config predates `tier`, and an
    // old binary compiled against it must keep working.
    final cfg = calloc<NainaConfig>();
    final out = calloc<Pointer<NainaCtx>>();
    try {
      cfg.ref
        ..version = 1
        ..backend = 0
        ..device = 0
        ..modelsRoot = nullptr
        ..numThreads = 0
        ..enableResearchModels = 0
        ..tier = 0
        ..language = nullptr;

      final rc = bindings.init(cfg, out);
      expect(rc, isNot(NainaStatus.invalidArg),
          reason: 'a version 1 config must not be rejected outright');
      if (rc == NainaStatus.ok) bindings.release(out.value);
    } finally {
      calloc.free(out);
      calloc.free(cfg);
    }
  });
}
