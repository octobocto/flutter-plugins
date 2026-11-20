#include "flutter_window.h"

#include <iostream>

FlutterWindow::FlutterWindow(const std::string& id,
                             const std::string& argument,
                             GtkWidget* window)
    : id_(id), window_argument_(argument), window_(window) {}

FlutterWindow::~FlutterWindow() = default;

void FlutterWindow::SetChannel(FlMethodChannel* channel) {
  channel_ = channel;
}

void FlutterWindow::NotifyWindowEvent(const gchar* event, FlValue* data) {
  if (channel_) {
    fl_method_channel_invoke_method(channel_, event, data, nullptr, nullptr, nullptr);
  }
}

void FlutterWindow::Show() {
  if (window_) {
    gtk_widget_show(GTK_WIDGET(window_));
  }
}

void FlutterWindow::Hide() {
  if (window_) {
    gtk_widget_hide(GTK_WIDGET(window_));
  }
}

void FlutterWindow::HandleWindowMethod(const gchar* method,
                                       FlValue* arguments,
                                       FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  if (strcmp(method, "window_show") == 0) {
    Show();
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "window_hide") == 0) {
    Hide();
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "window_set_frame") == 0) {
    GtkWindow* window = GTK_WINDOW(window_);
    gtk_window_move(
        window,
        static_cast<gint>(fl_value_get_float(fl_value_lookup_string(arguments, "x"))),
        static_cast<gint>(fl_value_get_float(fl_value_lookup_string(arguments, "y"))));
    gtk_window_resize(
        window,
        static_cast<gint>(fl_value_get_float(fl_value_lookup_string(arguments, "width"))),
        static_cast<gint>(fl_value_get_float(fl_value_lookup_string(arguments, "height"))));
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "window_set_title") == 0) {
    const gchar* title =
        fl_value_get_string(fl_value_lookup_string(arguments, "title"));
    GtkWindow* window = GTK_WINDOW(window_);
    gtk_window_set_title(window, title);
    GtkWidget* titlebar = gtk_window_get_titlebar(window);
    if (titlebar != nullptr && GTK_IS_HEADER_BAR(titlebar)) {
      gtk_header_bar_set_title(GTK_HEADER_BAR(titlebar), title);
    }
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else if (strcmp(method, "window_close") == 0) {
    gtk_window_close(GTK_WINDOW(window_));
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else {
    g_autofree gchar* error_msg = g_strdup_printf("unknown method: %s", method);
    response = FL_METHOD_RESPONSE(
        fl_method_error_response_new("-1", error_msg, nullptr));
  }

  fl_method_call_respond(method_call, response, nullptr);
}
