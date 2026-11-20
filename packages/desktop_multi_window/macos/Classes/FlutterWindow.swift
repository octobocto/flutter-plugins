import Cocoa
import FlutterMacOS
import Foundation

typealias WindowId = String

extension WindowId {
    static func generate() -> WindowId {
        return UUID().uuidString
    }
}

class CustomWindow: NSWindow {

    init(configuration: WindowConfiguration) {
        super.init(
            contentRect: NSRect(x: 10, y: 10, width: 800, height: 600),
            styleMask: [.miniaturizable, .closable, .titled, .resizable], backing: .buffered,
            defer: false)

        self.isReleasedWhenClosed = false
        self.animationBehavior = .none
    }

    /// When true, close hides the window and keeps its engine.
    var hidesOnClose = false
    var onHidden: (() -> Void)?

    override func close() {
        guard hidesOnClose else {
            super.close()
            return
        }
        orderOut(nil)
        onHidden?()
    }

    deinit {
        debugPrint("Child window deinit")
    }

}

class FlutterWindow: NSObject {
    let windowId: WindowId
    let windowArgument: String
    private(set) var window: NSWindow
    private var channel: FlutterMethodChannel?

    private var willBecomeActiveObserver: NSObjectProtocol?
    private var didResignActiveObserver: NSObjectProtocol?
    private var closeObserver: NSObjectProtocol?

    init(windowId: WindowId, windowArgument: String, window: NSWindow) {
        self.windowId = windowId
        self.windowArgument = windowArgument
        self.window = window
        super.init()

        willBecomeActiveObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.willBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            self?.didChangeOcclusionState(notification)
        }

        didResignActiveObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.didResignActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            self?.didChangeOcclusionState(notification)
        }

        (window as? CustomWindow)?.onHidden = { [weak self] in
            self?.notifyWindowEvent("onWindowHidden", data: [:])
        }

        closeObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: .main
        ) { [windowId] _ in
            MultiWindowManager.shared.removeWindow(windowId: windowId)
        }
    }

    deinit {
        if let willBecomeActiveObserver = willBecomeActiveObserver {
            NotificationCenter.default.removeObserver(willBecomeActiveObserver)
        }
        if let didResignActiveObserver = didResignActiveObserver {
            NotificationCenter.default.removeObserver(didResignActiveObserver)
        }
        if let closeObserver = closeObserver {
            NotificationCenter.default.removeObserver(closeObserver)
        }
    }

    @objc func didChangeOcclusionState(_ notification: Notification) {
        if let controller = window.contentViewController as? FlutterViewController {
            controller.engine.handleDidChangeOcclusionState(notification)
        }
    }

    func setChannel(_ channel: FlutterMethodChannel) {
        self.channel = channel
    }

    func notifyWindowEvent(_ event: String, data: [String: Any]) {
        if let channel = channel {
            channel.invokeMethod(event, arguments: data)
        } else {
            debugPrint("Channel not set for window \(windowId), cannot notify event \(event)")
        }
    }

    func handleWindowMethod(method: String, arguments: Any?, result: @escaping FlutterResult) {
        switch method {
        case "window_show":
            window.makeKeyAndOrderFront(nil)
            window.setIsVisible(true)
            NSApp.activate(ignoringOtherApps: true)
            result(nil)
        case "window_hide":
            window.orderOut(nil)
            result(nil)
        case "window_set_frame":
            let args = arguments as! [String: Any?]
            let x = args["x"] as! Double
            let y = args["y"] as! Double
            let width = args["width"] as! Double
            let height = args["height"] as! Double
            // Dart gives the top-left corner from the top of the primary screen.
            let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
            window.setFrame(
                NSRect(x: x, y: primaryHeight - y - height, width: width, height: height),
                display: true)
            result(nil)
        case "window_set_title":
            let args = arguments as! [String: Any?]
            window.title = args["title"] as! String
            result(nil)
        case "window_set_hide_on_close":
            let args = arguments as! [String: Any?]
            guard let customWindow = window as? CustomWindow else {
                result(FlutterError(code: "-1", message: "the main window cannot hide on close", details: nil))
                return
            }
            customWindow.hidesOnClose = args["hide"] as! Bool
            result(nil)
        case "window_close":
            window.close()
            result(nil)
        default:
            result(FlutterError(code: "-1", message: "unknown method \(method)", details: nil))
        }
    }

}
