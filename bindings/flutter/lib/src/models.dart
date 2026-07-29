// Fetching model weights on the device.
//
// The Android build of the core has no libcurl — the NDK ships none — so the core
// cannot download anything and Dart has to. Exactly the same split as the browser,
// where JS fetches through the Cache API.
//
// What Dart does NOT do is decide where files go or which are needed. That comes
// from naina_staging_plan in the C core, which owns the cache layout and the
// ${release_base} substitution. Deriving paths here would be a second source of
// truth, and the sha256 the core verifies is baked into the path.

import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart' show rootBundle;

import 'bindings.dart';

/// One weight file the core needs.
class ModelFile {
  const ModelFile({required this.path, required this.url, required this.bytes});

  /// Absolute path the core expects this file at.
  final String path;

  /// Where to download it from.
  final String url;

  /// Expected size, or 0 when the registry does not record one.
  final int bytes;

  String get name => path.substring(path.lastIndexOf('/') + 1);

  /// The filename with the cache layout's sha256 prefix stripped, for display.
  String get displayName => name.replaceFirst(RegExp(r'^[0-9a-f]{16}__'), '');
}

/// Progress while staging.
class StagingProgress {
  const StagingProgress({
    required this.fileIndex,
    required this.fileCount,
    required this.file,
    required this.receivedBytes,
    required this.totalBytes,
  });

  final int fileIndex;
  final int fileCount;
  final ModelFile file;
  final int receivedBytes;

  /// Total for the current file, or 0 when the server sends no length.
  final int totalBytes;

  /// Fraction of the whole job, counting files rather than bytes when a file's
  /// size is unknown.
  double get fraction {
    if (fileCount == 0) return 1;
    final within = totalBytes > 0 ? receivedBytes / totalBytes : 0.0;
    return (fileIndex + within.clamp(0.0, 1.0)) / fileCount;
  }
}

/// Downloads the weights a tier and language need.
///
/// Call once before [Naina.open]; on later runs it finds the files already
/// present and returns immediately.
///
/// ```dart
/// final dir = await getApplicationSupportDirectory();
/// final root = '${dir.path}/naina';
/// await NainaModels.stage(modelsRoot: root, onProgress: (p) => print(p.fraction));
/// final naina = await Naina.open(modelsRoot: root);
/// ```
class NainaModels {
  NainaModels._();

  /// Where the registry is copied to inside [modelsRoot].
  ///
  /// naina_init resolves `<models_root>/registry.yaml`, so it must land here
  /// under exactly this name.
  static const registryFileName = 'registry.yaml';

  /// The registry shipped as a package asset.
  static const _registryAsset = 'packages/naina/assets/registry.yaml';

  /// Copy the bundled registry into [modelsRoot] and return its path.
  ///
  /// The core needs the registry before it can say which weights it wants, so
  /// this is unavoidably first. It is a few KB and ships in the package rather
  /// than being downloaded, so a first run needs one fewer round trip and cannot
  /// half-fail with a registry but no models.
  static Future<String> installRegistry(String modelsRoot) async {
    final dir = Directory(modelsRoot);
    if (!dir.existsSync()) {
      await dir.create(recursive: true);
    }
    final dest = File('$modelsRoot/$registryFileName');
    final data = await rootBundle.load(_registryAsset);
    await dest.writeAsBytes(data.buffer.asUint8List(), flush: true);
    return dest.path;
  }

  /// Ask the core which files are needed.
  ///
  /// Throws [NainaException] with [NainaStatus.unsupported] for a language the
  /// registry does not describe — deliberately, rather than returning a plan
  /// with no recogniser in it.
  static List<ModelFile> plan({
    required String registryPath,
    required String modelsRoot,
    int tier = NainaTier.tiny,
    String? language,
  }) {
    final bindings = NainaBindings(openNainaLibrary());

    final regPtr = registryPath.toNativeUtf8();
    final rootPtr = modelsRoot.toNativeUtf8();
    final langPtr =
        (language != null && language.isNotEmpty) ? language.toNativeUtf8() : nullptr;
    final outPtr = calloc<Pointer<Utf8>>();
    try {
      final rc = bindings.stagingPlan(regPtr, rootPtr, tier, langPtr, outPtr);
      if (rc != NainaStatus.ok) {
        throw NainaException(rc, bindings.statusStr(rc).toDartString());
      }
      final json = outPtr.value.toDartString();
      // Free through the core's own allocator, not calloc: it was malloc'd on
      // the C side and the two heaps are not interchangeable.
      bindings.freeString(outPtr.value);

      return (jsonDecode(json) as List<dynamic>)
          .cast<Map<String, dynamic>>()
          .map((m) => ModelFile(
                path: m['path'] as String,
                url: m['url'] as String,
                bytes: (m['bytes'] as num).toInt(),
              ))
          .toList();
    } finally {
      calloc.free(outPtr);
      if (langPtr != nullptr) calloc.free(langPtr);
      calloc.free(rootPtr);
      calloc.free(regPtr);
    }
  }

  /// Install the registry, then download whatever is missing.
  ///
  /// Returns the paths staged. Already-present files of the expected size are
  /// skipped: the URLs point at an immutable, sha256-pinned release, so a
  /// size match means the bytes are right — and the core hashes them anyway.
  static Future<List<String>> stage({
    required String modelsRoot,
    int tier = NainaTier.tiny,
    String? language,
    void Function(StagingProgress)? onProgress,
    HttpClient? client,
  }) async {
    final registryPath = await installRegistry(modelsRoot);
    // modelsRoot must be the same value Naina.open receives, or files land
    // where the core will not look for them.
    final files = plan(
      registryPath: registryPath,
      modelsRoot: modelsRoot,
      tier: tier,
      language: language,
    );

    final http = client ?? HttpClient();
    final staged = <String>[];
    try {
      for (var i = 0; i < files.length; i++) {
        final f = files[i];
        final dest = File(f.path);

        if (dest.existsSync() && (f.bytes == 0 || dest.lengthSync() == f.bytes)) {
          staged.add(f.path);
          onProgress?.call(StagingProgress(
            fileIndex: i,
            fileCount: files.length,
            file: f,
            receivedBytes: f.bytes,
            totalBytes: f.bytes,
          ));
          continue;
        }

        await dest.parent.create(recursive: true);

        final req = await http.getUrl(Uri.parse(f.url));
        final res = await req.close();
        if (res.statusCode != HttpStatus.ok) {
          throw NainaException(
            NainaStatus.io,
            'HTTP ${res.statusCode} fetching ${f.displayName}',
          );
        }

        // Write to a .part and rename, so an interrupted download cannot leave a
        // truncated file that looks staged on the next run.
        final part = File('${f.path}.part');
        final sink = part.openWrite();
        var received = 0;
        final total = res.contentLength > 0 ? res.contentLength : f.bytes;
        try {
          await res.forEach((chunk) {
            sink.add(chunk);
            received += chunk.length;
            onProgress?.call(StagingProgress(
              fileIndex: i,
              fileCount: files.length,
              file: f,
              receivedBytes: received,
              totalBytes: total,
            ));
          });
        } finally {
          await sink.close();
        }

        if (f.bytes > 0 && await part.length() != f.bytes) {
          final got = await part.length();
          await part.delete();
          throw NainaException(
            NainaStatus.io,
            '${f.displayName}: got $got bytes, registry says ${f.bytes}',
          );
        }
        await part.rename(f.path);
        staged.add(f.path);
      }
    } finally {
      if (client == null) http.close();
    }
    return staged;
  }
}
