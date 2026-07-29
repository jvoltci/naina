// FFI bindings to naina's C ABI.
//
// Hand-written rather than ffigen-generated: the surface needed for text
// extraction is small, and a hand-written file can carry the ownership rules
// that a generated one drops. Every function here mirrors a declaration in
// core/include/naina/naina.h — if that header changes, this must follow.
//
// Ownership, which the C types cannot express:
//   * naina_page_t owns every string it hands out. A `text` pointer dangles the
//     moment nainaPageRelease is called, so any Dart String must be copied out
//     before then.
//   * naina_image_t is a VIEW over caller pixels. The bytes must outlive it.

import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

// ─── enums, mirrored from naina.h ──────────────────────────────────────

abstract final class NainaStatus {
  static const int ok = 0;
  static const int invalidArg = 1;
  static const int notInitialized = 2;
  static const int modelNotFound = 3;
  static const int backendUnavail = 4;
  static const int inferenceFailed = 5;
  static const int oom = 6;
  static const int unsupported = 7;
  static const int io = 8;
}

abstract final class NainaTier {
  static const int auto = 0;
  static const int tiny = 1;
  static const int small = 2;
  static const int medium = 3;
}

abstract final class NainaPixfmt {
  static const int rgb8 = 0;
  static const int bgr8 = 1;
  static const int nv12 = 2;
  static const int yuv420p = 3;
  static const int gray8 = 4;
}

// ─── structs ───────────────────────────────────────────────────────────

final class NainaPoint extends Struct {
  @Float()
  external double x;
  @Float()
  external double y;
}

/// Corners are clockwise from top-left, in source-image pixels. Quads are not
/// necessarily axis-aligned: rotated text produces genuinely rotated quads.
final class NainaTextbox extends Struct {
  @Array(4)
  external Array<NainaPoint> corners;
  @Float()
  external double score;
}

final class NainaTextline extends Struct {
  external NainaTextbox box;

  /// Owned by the page. Dangles after nainaPageRelease.
  external Pointer<Utf8> text;

  @Float()
  external double confidence;

  @Int32()
  external int regionId;
}

/// Field order and types must match `naina_config` exactly. `version` is the
/// ABI gate: 2 is what this binding writes, and the core still accepts 1.
final class NainaConfig extends Struct {
  @Int32()
  external int version;
  @Int32()
  external int backend;
  @Int32()
  external int device;
  external Pointer<Utf8> modelsRoot;
  @Int32()
  external int numThreads;
  @Int32()
  external int enableResearchModels;
  @Int32()
  external int tier;

  /// Recognition alphabet, e.g. "devanagari". nullptr or "" is the default
  /// (Latin + CJK). Honoured when [version] >= 3.
  external Pointer<Utf8> language;
}

// Opaque handles.
final class NainaCtx extends Opaque {}

final class NainaImage extends Opaque {}

final class NainaPage extends Opaque {}

// ─── signatures ────────────────────────────────────────────────────────

typedef NainaInitNative = Int32 Function(
    Pointer<NainaConfig>, Pointer<Pointer<NainaCtx>>);
typedef NainaInitDart = int Function(
    Pointer<NainaConfig>, Pointer<Pointer<NainaCtx>>);

typedef NainaReleaseNative = Void Function(Pointer<NainaCtx>);
typedef NainaReleaseDart = void Function(Pointer<NainaCtx>);

typedef NainaImageWrapNative = Int32 Function(Pointer<Uint8>, Int32, Int32,
    Int32, Int32, Pointer<Pointer<NainaImage>>);
typedef NainaImageWrapDart = int Function(Pointer<Uint8>, int, int, int, int,
    Pointer<Pointer<NainaImage>>);

typedef NainaImageReleaseNative = Void Function(Pointer<NainaImage>);
typedef NainaImageReleaseDart = void Function(Pointer<NainaImage>);

typedef NainaReadNative = Int32 Function(
    Pointer<NainaCtx>, Pointer<NainaImage>, Pointer<Pointer<NainaPage>>);
typedef NainaReadDart = int Function(
    Pointer<NainaCtx>, Pointer<NainaImage>, Pointer<Pointer<NainaPage>>);

typedef NainaPageReleaseNative = Void Function(Pointer<NainaPage>);
typedef NainaPageReleaseDart = void Function(Pointer<NainaPage>);

typedef NainaPageLinesNative = Int32 Function(Pointer<NainaPage>,
    Pointer<Pointer<NainaTextline>>, Pointer<Int32>);
typedef NainaPageLinesDart = int Function(Pointer<NainaPage>,
    Pointer<Pointer<NainaTextline>>, Pointer<Int32>);

typedef NainaStrNative = Pointer<Utf8> Function(Pointer<NainaPage>);
typedef NainaStrDart = Pointer<Utf8> Function(Pointer<NainaPage>);

typedef NainaVersionNative = Pointer<Utf8> Function();
typedef NainaVersionDart = Pointer<Utf8> Function();

typedef NainaStatusStrNative = Pointer<Utf8> Function(Int32);
typedef NainaStatusStrDart = Pointer<Utf8> Function(int);

typedef NainaStagingPlanNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Int32, Pointer<Utf8>, Pointer<Pointer<Utf8>>);
typedef NainaStagingPlanDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Pointer<Utf8>>);

typedef NainaFreeStringNative = Void Function(Pointer<Utf8>);
typedef NainaFreeStringDart = void Function(Pointer<Utf8>);

/// Open libnaina for the current platform.
///
/// Android packages the .so into the APK, so it resolves by name. iOS links it
/// into the app binary, so the process itself exports the symbols.
DynamicLibrary openNainaLibrary() {
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('libnaina.so');
  }
  if (Platform.isIOS || Platform.isMacOS) return DynamicLibrary.process();
  if (Platform.isWindows) return DynamicLibrary.open('naina.dll');
  throw UnsupportedError('naina: unsupported platform ${Platform.operatingSystem}');
}

/// Resolved symbols from libnaina.
///
/// Looked up eagerly in the constructor so a version mismatch fails at load
/// with a clear symbol name, rather than at the first read with a segfault.
class NainaBindings {
  NainaBindings(DynamicLibrary lib)
      : init = lib.lookupFunction<NainaInitNative, NainaInitDart>('naina_init'),
        release =
            lib.lookupFunction<NainaReleaseNative, NainaReleaseDart>('naina_release'),
        imageWrap = lib.lookupFunction<NainaImageWrapNative, NainaImageWrapDart>(
            'naina_image_wrap'),
        imageRelease =
            lib.lookupFunction<NainaImageReleaseNative, NainaImageReleaseDart>(
                'naina_image_release'),
        read = lib.lookupFunction<NainaReadNative, NainaReadDart>('naina_read'),
        pageRelease =
            lib.lookupFunction<NainaPageReleaseNative, NainaPageReleaseDart>(
                'naina_page_release'),
        pageLines = lib.lookupFunction<NainaPageLinesNative, NainaPageLinesDart>(
            'naina_page_lines'),
        pageMarkdown = lib.lookupFunction<NainaStrNative, NainaStrDart>(
            'naina_page_markdown'),
        pageJson =
            lib.lookupFunction<NainaStrNative, NainaStrDart>('naina_page_json'),
        versionString = lib.lookupFunction<NainaVersionNative, NainaVersionDart>(
            'naina_version_string'),
        statusStr = lib.lookupFunction<NainaStatusStrNative, NainaStatusStrDart>(
            'naina_status_str'),
        stagingPlan =
            lib.lookupFunction<NainaStagingPlanNative, NainaStagingPlanDart>(
                'naina_staging_plan'),
        freeString = lib.lookupFunction<NainaFreeStringNative, NainaFreeStringDart>(
            'naina_free_string');

  final NainaInitDart init;
  final NainaReleaseDart release;
  final NainaImageWrapDart imageWrap;
  final NainaImageReleaseDart imageRelease;
  final NainaReadDart read;
  final NainaPageReleaseDart pageRelease;
  final NainaPageLinesDart pageLines;
  final NainaStrDart pageMarkdown;
  final NainaStrDart pageJson;
  final NainaVersionDart versionString;
  final NainaStatusStrDart statusStr;
  final NainaStagingPlanDart stagingPlan;
  final NainaFreeStringDart freeString;
}

/// Thrown when the native library reports a failure.
///
/// Declared here rather than in naina.dart so src/models.dart can throw it
/// without importing the public library and creating a cycle.
class NainaException implements Exception {
  const NainaException(this.status, this.message);
  final int status;
  final String message;

  @override
  String toString() => 'NainaException(\$status): \$message';
}
