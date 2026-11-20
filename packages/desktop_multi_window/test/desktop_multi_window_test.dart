import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  final calls = <MethodCall>[];

  setUp(() {
    calls.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      const MethodChannel('mixin.one/desktop_multi_window'),
      (call) async {
        calls.add(call);
        return null;
      },
    );
  });

  test('window controls address the window by id', () async {
    final controller = WindowController.fromWindowId('w1');

    await controller.setFrame(const Rect.fromLTWH(10, 20, 300, 400));
    await controller.setTitle('Address Book');
    await controller.close();

    expect(calls.map((c) => c.method), [
      'window_set_frame',
      'window_set_title',
      'window_close',
    ]);
    expect(calls[0].arguments, {
      'windowId': 'w1',
      'x': 10.0,
      'y': 20.0,
      'width': 300.0,
      'height': 400.0,
    });
    expect(calls[1].arguments, {'windowId': 'w1', 'title': 'Address Book'});
    expect(calls[2].arguments, {'windowId': 'w1'});
  });
}
