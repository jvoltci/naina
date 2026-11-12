#
# naina — iOS. Text extraction over FFI.
#
Pod::Spec.new do |s|
  s.name             = 'naina'
  s.version          = '0.2.0'
  s.summary          = 'On-device OCR from naina\'s C++ core.'
  s.description      = 'Reads text from images entirely on device. No network calls.'
  s.homepage         = 'https://github.com/jvoltci/naina'
  s.license          = { :type => 'Apache-2.0', :file => '../LICENSE' }
  s.author           = { 'jvoltci' => 'https://github.com/jvoltci' }
  s.source           = { :path => '.' }
  s.platform         = :ios, '13.0'
  s.dependency 'Flutter'

  # onnxruntime-c is Microsoft's official CocoaPod and carries the arm64 device
  # and simulator slices.
  s.dependency 'onnxruntime-c', '~> 1.20.0'

  s.source_files     = 'Classes/**/*'

  # Symbols must survive into the app binary: Dart resolves them with
  # DynamicLibrary.process(), which finds nothing if the linker strips them.
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'STRIP_STYLE' => 'non-global',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
  }
  s.libraries = 'c++'
end
