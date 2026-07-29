// Registration stub. naina is an FFI plugin: Dart calls the C ABI directly and
// there is no method channel. This header exists so the Flutter tool has a
// pluginClass to register, which is what makes the native build run.
#import <Flutter/Flutter.h>

@interface NainaPlugin : NSObject <FlutterPlugin>
@end
