#include "multi_window_manager.h"

#include <iomanip>
#include <random>
#include <sstream>

#include "desktop_multi_window_plugin_internal.h"
#include "flutter_window.h"
#include "include/desktop_multi_window/desktop_multi_window_plugin.h"
#include "window_configuration.h"
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

namespace {

std::string GenerateWindowId() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;

  uint64_t part1 = dis(gen);
  uint64_t part2 = dis(gen);

  part1 = (part1 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  part2 = (part2 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  char uuid_str[37];
  snprintf(uuid_str, sizeof(uuid_str), "%08x-%04x-%04x-%04x-%012llx",
           static_cast<uint32_t>(part1 >> 32),
           static_cast<uint16_t>(part1 >> 16), static_cast<uint16_t>(part1),
           static_cast<uint16_t>(part2 >> 48), part2 & 0xFFFFFFFFFFFFULL);

  return std::string(uuid_str);
}

WindowCreatedCallback _g_window_created_callback = nullptr;

}  // namespace

// static
MultiWindowManager* MultiWindowManager::Instance() {
  static auto manager = std::make_shared<MultiWindowManager>();
  return manager.get();
}

MultiWindowManager::MultiWindowManager() : windows_() {}

MultiWindowManager::~MultiWindowManager() = default;

std::string MultiWindowManager::Create(FlValue* args) {
  WindowConfiguration config = WindowConfiguration::FromFlValue(args);
  std::string window_id = GenerateWindowId();

  // Create a plain GtkWindow (NOT GtkApplicationWindow) for sub-windows.
  // GtkApplicationWindows are tracked by GtkApplication, and closing one can
  // trigger the application to quit. Plain GtkWindows are independent.
  // See: https://github.com/MixinNetwork/flutter-plugins/issues/457
  GtkWindow* window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));

  gboolean use_header_bar = TRUE;
#ifdef GDK_WINDOWING_X11
  GdkScreen* screen = gtk_window_get_screen(window);
  if (GDK_IS_X11_SCREEN(screen)) {
    const gchar* wm_name = gdk_x11_screen_get_window_manager_name(screen);
    if (g_strcmp0(wm_name, "GNOME Shell") != 0) {
      use_header_bar = FALSE;
    }
  }
#endif
  if (use_header_bar) {
    GtkHeaderBar* header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
    gtk_widget_show(GTK_WIDGET(header_bar));
    gtk_header_bar_set_title(header_bar, "");
    gtk_header_bar_set_show_close_button(header_bar, TRUE);
    gtk_window_set_titlebar(window, GTK_WIDGET(header_bar));
  } else {
    gtk_window_set_title(window, "");
  }

  gtk_window_set_default_size(window, 1280, 720);

  gtk_window_set_title(window, "");
  if (config.hidden_at_launch) {
    gtk_widget_realize(GTK_WIDGET(window));
  } else {
    gtk_widget_show(GTK_WIDGET(window));
  }

  // Create FlutterWindow instance
  auto w = std::make_unique<FlutterWindow>(window_id, config.arguments,
                                           GTK_WIDGET(window));
  windows_[window_id] = std::move(w);

  // Connect our delete-event handler BEFORE creating the FlView.
  // GLib signal handlers run in connection order, so ours fires first.
  // For sub-windows, our handler returns TRUE to stop propagation, preventing
  // the Flutter engine's handler (which may quit the app) from running.
  ObserveWindowClose(window_id, window, /* is_sub_window= */ true);

  // Setup Flutter project
  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_enable_impeller(project, FALSE);
  const char* entrypoint_args[] = {"multi_window", window_id.c_str(),
                                   config.arguments.c_str(), nullptr};
  fl_dart_project_set_dart_entrypoint_arguments(
      project, const_cast<char**>(entrypoint_args));

  // Create Flutter view
  auto fl_view = fl_view_new(project);
  gtk_widget_show(GTK_WIDGET(fl_view));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(fl_view));

  // Call window created callback
  if (_g_window_created_callback) {
    _g_window_created_callback(FL_PLUGIN_REGISTRY(fl_view));
  }
  // Register plugin
  g_autoptr(FlPluginRegistrar) desktop_multi_window_registrar =
      fl_plugin_registry_get_registrar_for_plugin(FL_PLUGIN_REGISTRY(fl_view),
                                                  "DesktopMultiWindowPlugin");

  desktop_multi_window_plugin_register_with_registrar_internal(
      desktop_multi_window_registrar, windows_[window_id].get());

  gtk_widget_grab_focus(GTK_WIDGET(fl_view));

  // Notify all windows about the change
  NotifyWindowsChanged();

  return window_id;
}

void MultiWindowManager::AttachMainWindow(GtkWidget* window_widget,
                                          FlPluginRegistrar* registrar) {
  // check window widget is in windows_
  for (const auto& pair : windows_) {
    if (pair.second->GetWindow() == GTK_WINDOW(window_widget)) {
      return;
    }
  }

  const std::string main_window_id = GenerateWindowId();
  auto window =
      std::make_unique<FlutterWindow>(main_window_id, "", window_widget);
  windows_[main_window_id] = std::move(window);

  ObserveWindowClose(main_window_id, GTK_WINDOW(window_widget),
                     /* is_sub_window= */ false);
  desktop_multi_window_plugin_register_with_registrar_internal(
      registrar, windows_[main_window_id].get());

  // Notify all windows about the change
  NotifyWindowsChanged();
}

void MultiWindowManager::ObserveWindowClose(const std::string& window_id,
                                            GtkWindow* window,
                                            bool is_sub_window) {
  if (is_sub_window) {
    // Sub-window close handler.
    // Return TRUE to stop signal propagation. This prevents:
    // 1. The Flutter engine's delete-event handler from calling
    //    g_application_quit() (if one is registered).
    // 2. The default handler from destroying the window, which would trigger
    //    FlView unrealize and cause a GLX BadAccess crash due to GL context
    //    conflicts with the main window.
    // Instead, we just hide the window. It will be fully cleaned up when the
    // application exits.
    // See: https://github.com/MixinNetwork/flutter-plugins/issues/457
    g_signal_connect(
        GTK_WIDGET(window), "delete-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, gpointer arg) -> gboolean {
          auto* window_id_ptr = static_cast<std::string*>(arg);
          MultiWindowManager::Instance()->RemoveWindow(*window_id_ptr);
          delete window_id_ptr;
          gtk_widget_hide(widget);
          return TRUE;
        }),
        new std::string(window_id));
  } else {
    // Main window: allow normal destruction so the app quits.
    g_signal_connect(
        GTK_WIDGET(window), "delete-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, gpointer arg) -> gboolean {
          auto* window_id_ptr = static_cast<std::string*>(arg);
          MultiWindowManager::Instance()->RemoveWindow(*window_id_ptr);
          delete window_id_ptr;
          return FALSE;
        }),
        new std::string(window_id));
  }
}

FlutterWindow* MultiWindowManager::GetWindow(const std::string& window_id) {
  auto it = windows_.find(window_id);
  if (it != windows_.end()) {
    return it->second.get();
  }
  return nullptr;
}

FlValue* MultiWindowManager::GetAllWindows() {
  g_autoptr(FlValue) windows = fl_value_new_list();
  for (const auto& pair : windows_) {
    g_autoptr(FlValue) window_info = fl_value_new_map();
    fl_value_set_string_take(
        window_info, "windowId",
        fl_value_new_string(pair.second->GetWindowId().c_str()));
    fl_value_set_string_take(
        window_info, "windowArgument",
        fl_value_new_string(pair.second->GetWindowArgument().c_str()));
    fl_value_append_take(windows, fl_value_ref(window_info));
  }
  return fl_value_ref(windows);
}

std::vector<std::string> MultiWindowManager::GetAllWindowIds() {
  std::vector<std::string> window_ids;
  for (const auto& pair : windows_) {
    window_ids.push_back(pair.first);
  }
  return window_ids;
}

void MultiWindowManager::NotifyWindowsChanged() {
  auto window_ids = GetAllWindowIds();

  g_autoptr(FlValue) window_ids_list = fl_value_new_list();
  for (const auto& id : window_ids) {
    fl_value_append_take(window_ids_list, fl_value_new_string(id.c_str()));
  }

  g_autoptr(FlValue) data = fl_value_new_map();
  fl_value_set_string_take(data, "windowIds", fl_value_ref(window_ids_list));

  for (const auto& pair : windows_) {
    pair.second->NotifyWindowEvent("onWindowsChanged", data);
  }
}

void MultiWindowManager::RemoveWindow(const std::string& window_id) {
  g_warning("RemoveWindow: %s", window_id.c_str());
  windows_.erase(window_id);
  NotifyWindowsChanged();
}

void desktop_multi_window_plugin_set_window_created_callback(
    WindowCreatedCallback callback) {
  _g_window_created_callback = callback;
}
