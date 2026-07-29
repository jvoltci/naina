// Minimal naina example: pick an image, read its text on-device.
//
// The parts worth copying are decodeToRgb (Flutter gives you RGBA, naina wants
// RGB) and the modelsRoot choice — an app sandbox cannot write to the default
// cache location, so it must be told where to put the weights.

import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';
import 'package:naina/naina.dart';
import 'package:path_provider/path_provider.dart';

void main() => runApp(const NainaExampleApp());

class NainaExampleApp extends StatelessWidget {
  const NainaExampleApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'naina',
        theme: ThemeData.dark(useMaterial3: true),
        home: const ReaderPage(),
      );
}

class ReaderPage extends StatefulWidget {
  const ReaderPage({super.key});

  @override
  State<ReaderPage> createState() => _ReaderPageState();
}

class _ReaderPageState extends State<ReaderPage> {
  String _status = 'Pick an image to read.';
  String _text = '';
  bool _busy = false;

  /// Flutter's decoder gives RGBA; naina takes packed RGB.
  ///
  /// Decode at native resolution and let naina resize — its resize is the same
  /// code on every platform, so scaling here would make results device-dependent.
  static Future<(Uint8List, int, int)> _decodeToRgb(Uint8List fileBytes) async {
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
    final result = (rgb, image.width, image.height);
    image.dispose();
    return result;
  }

  Future<void> _pickAndRead() async {
    if (_busy) return;
    setState(() {
      _busy = true;
      _text = '';
      _status = 'Choosing…';
    });

    try {
      final picked = await ImagePicker().pickImage(source: ImageSource.gallery);
      if (picked == null) {
        setState(() => _status = 'Nothing chosen.');
        return;
      }

      setState(() => _status = 'Decoding…');
      final (rgb, width, height) = await _decodeToRgb(await picked.readAsBytes());

      // The app sandbox cannot write to naina's default cache location, so give
      // it somewhere inside our own container.
      final dir = await getApplicationSupportDirectory();
      final root = '${dir.path}/naina';

      // Android's core has no libcurl, so Dart fetches the weights. Returns
      // immediately once they are on disk.
      await NainaModels.stage(
        modelsRoot: root,
        onProgress: (p) => setState(() => _status =
            'Fetching ${p.file.displayName} — ${(p.fraction * 100).toStringAsFixed(0)}%'),
      );

      setState(() => _status = 'Reading…');
      final page = await readRgbInIsolate(
        rgb,
        width: width,
        height: height,
        modelsRoot: root,
      );

      setState(() {
        _text = page.text;
        _status = page.lines.isEmpty
            ? 'No text found.'
            : '${page.lines.length} lines · '
                'mean confidence ${page.meanConfidence.toStringAsFixed(2)}';
      });
    } on NainaException catch (e) {
      setState(() => _status = 'naina failed: ${e.message}');
    } catch (e) {
      setState(() => _status = 'Failed: $e');
    } finally {
      setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(title: const Text('naina')),
        body: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              FilledButton.icon(
                onPressed: _busy ? null : _pickAndRead,
                icon: const Icon(Icons.image_outlined),
                label: const Text('Pick an image'),
              ),
              const SizedBox(height: 12),
              if (_busy) const LinearProgressIndicator(),
              const SizedBox(height: 12),
              Text(_status, style: Theme.of(context).textTheme.bodySmall),
              const SizedBox(height: 12),
              Expanded(
                child: SingleChildScrollView(
                  child: SelectableText(
                    _text,
                    style: const TextStyle(fontFamily: 'monospace', fontSize: 13),
                  ),
                ),
              ),
            ],
          ),
        ),
      );
}
