#ifndef DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_WRAPPER_H_
#define DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_WRAPPER_H_

#include <Windows.h>
#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <flutter/method_result.h>
#include <memory>
#include <string>
#include <variant>

class FlutterWindowWrapper {
 public:
  FlutterWindowWrapper(const std::string& window_id,
                       HWND hwnd,
                       const std::string& window_argument = "")
      : window_id_(window_id), hwnd_(hwnd), window_argument_(window_argument) {}

  ~FlutterWindowWrapper() = default;

  std::string GetWindowId() const { return window_id_; }

  std::string GetWindowArgument() const { return window_argument_; }

  HWND GetWindowHandle() { return hwnd_; }

  void SetChannel(
      std::shared_ptr<flutter::MethodChannel<flutter::EncodableValue>>
          channel) {
    channel_ = channel;
  }

  void NotifyWindowEvent(const std::string& event,
                         const flutter::EncodableMap& data) {
    if (channel_) {
      channel_->InvokeMethod(event,
                             std::make_unique<flutter::EncodableValue>(data));
    }
  }

  void HandleWindowMethod(
      const std::string& method,
      const flutter::EncodableMap* arguments,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (method == "window_show") {
      if (hwnd_) {
        ::ShowWindow(hwnd_, SW_SHOW);
      }
      result->Success();
    } else if (method == "window_hide") {
      if (hwnd_) {
        ::ShowWindow(hwnd_, SW_HIDE);
      }
      result->Success();
    } else if (method == "window_set_frame") {
      if (hwnd_) {
        // Dart gives logical pixels; Win32 wants physical ones.
        const double scale = ::GetDpiForWindow(hwnd_) / 96.0;
        ::SetWindowPos(hwnd_, nullptr,
                       static_cast<int>(GetDouble(arguments, "x") * scale),
                       static_cast<int>(GetDouble(arguments, "y") * scale),
                       static_cast<int>(GetDouble(arguments, "width") * scale),
                       static_cast<int>(GetDouble(arguments, "height") * scale),
                       SWP_NOZORDER | SWP_NOACTIVATE);
      }
      result->Success();
    } else if (method == "window_set_title") {
      if (hwnd_) {
        const auto& title = std::get<std::string>(
            arguments->at(flutter::EncodableValue("title")));
        const int length = ::MultiByteToWideChar(
            CP_UTF8, 0, title.c_str(), static_cast<int>(title.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, title.c_str(),
                              static_cast<int>(title.size()), wide.data(), length);
        ::SetWindowTextW(hwnd_, wide.c_str());
      }
      result->Success();
    } else if (method == "window_close") {
      if (hwnd_) {
        ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
      }
      result->Success();
    } else {
      result->Error("-1", "unknown method: " + method);
    }
  }

 protected:
  void SetWindowHandle(HWND hwnd) { hwnd_ = hwnd; }

 private:
  static double GetDouble(const flutter::EncodableMap* arguments,
                          const char* key) {
    return std::get<double>(arguments->at(flutter::EncodableValue(key)));
  }

 private:
  std::string window_id_;
  HWND hwnd_;
  std::string window_argument_;
  std::shared_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

#endif  // DESKTOP_MULTI_WINDOW_WINDOWS_FLUTTER_WINDOW_WRAPPER_H_
