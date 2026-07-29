package com.jvoltci.naina

import io.flutter.embedding.engine.plugins.FlutterPlugin

/**
 * Registration stub.
 *
 * naina is an FFI plugin: Dart calls the C ABI directly and no method channel is
 * involved. This class exists only so the Flutter tool has a `pluginClass` to
 * register, which is what causes the native build to run and libnaina.so to be
 * packaged into the APK.
 */
class NainaPlugin : FlutterPlugin {
    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) = Unit
    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) = Unit
}
