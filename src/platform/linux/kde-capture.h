/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native KDE virtual capture. Authorization is the app's exported desktop
 * interface declaration; no portal picker and no global permission bypass.
 */
#pragma once
#include <wayland-client.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include "zkde-screencast-unstable-v1.h"
namespace portal {
class kde_virtual_t {
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  zkde_screencast_unstable_v1 *manager = nullptr;
  zkde_screencast_stream_unstable_v1 *stream = nullptr;
  bool failed = false;
  static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    auto self = static_cast<kde_virtual_t *>(data);
    if (!std::strcmp(interface, "zkde_screencast_unstable_v1") && version >= 2)
      self->manager = static_cast<zkde_screencast_unstable_v1 *>(wl_registry_bind(registry, name, &zkde_screencast_unstable_v1_interface, 2));
  }
  static void removed(void *, wl_registry *, uint32_t) {}
  static void synced(void *data, wl_callback *callback, uint32_t) { *static_cast<bool *>(data) = true; wl_callback_destroy(callback); }
  bool pump_until(const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done() && !failed) {
      if (wl_display_dispatch_pending(display) < 0) return false;
      if (done()) break;
      if (wl_display_flush(display) < 0 && errno != EAGAIN) return false;
      auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (remaining <= 0) return false;
      pollfd pfd {wl_display_get_fd(display), POLLIN, 0};
      int ready = poll(&pfd, 1, static_cast<int>(remaining));
      if (ready < 0 && errno == EINTR) continue;
      if (ready <= 0 || !(pfd.revents & POLLIN) || wl_display_dispatch(display) < 0) return false;
    }
    return !failed;
  }
public:
  uint32_t node = PW_ID_ANY;
  std::string output_name;
  ~kde_virtual_t() {
    if (stream) zkde_screencast_stream_unstable_v1_close(stream);
    if (manager) zkde_screencast_unstable_v1_destroy(manager);
    if (registry) wl_registry_destroy(registry);
    if (display) { wl_display_flush(display); wl_display_disconnect(display); }
  }
  bool available() {
    display = wl_display_connect(nullptr);
    if (!display) return false;
    registry = wl_display_get_registry(display);
    static const wl_registry_listener listener {global, removed};
    wl_registry_add_listener(registry, &listener, this);
    bool done = false;
    auto sync = wl_display_sync(display);
    static const wl_callback_listener callback {synced};
    wl_callback_add_listener(sync, &callback, &done);
    if (!pump_until([&] { return done; })) {
      if (!done) wl_callback_destroy(sync);
      return false;
    }
    return manager;
  }
  // suffix: a distinct output (e.g. the capability probe) that KWin's stored layout for the
  // stream's output name never applies to.
  bool open(int width, int height, const char *suffix = "") {
    if (!available()) return false;
    const char *app = std::getenv("FLATPAK_ID");
    const auto name = std::string("virtual-umbra-") + (app ? app : "host") + suffix;
    output_name = "Virtual-" + name;
    stream = zkde_screencast_unstable_v1_stream_virtual_output(manager, name.c_str(), width, height, wl_fixed_from_int(1), 2);
    static const zkde_screencast_stream_unstable_v1_listener listener {
      [](void *p, auto *) { static_cast<kde_virtual_t *>(p)->failed = true; },
      [](void *p, auto *, uint32_t node) { static_cast<kde_virtual_t *>(p)->node = node; },
      [](void *p, auto *, const char *detail) {
        static_cast<kde_virtual_t *>(p)->failed = true;
        BOOST_LOG(error) << "KDE native virtual capture: " << detail;
      }
    };
    zkde_screencast_stream_unstable_v1_add_listener(stream, &listener, this);
    return pump_until([&] { return node != PW_ID_ANY; });
  }
  int open_remote() {
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) return -1;
    auto path = std::string(runtime) + "/pipewire-0";
    sockaddr_un address {};
    if (path.size() >= sizeof address.sun_path) return -1;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) { close(fd); return -1; }
    return fd;
  }
};
}
