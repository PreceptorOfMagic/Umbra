/* SPDX-License-Identifier: GPL-3.0-or-later
 * GNOME per-client virtual display: Mutter ScreenCast RecordVirtual (no prompt), sized by a short
 * PipeWire negotiation of the client's WxH and rate, verified and made the only active display
 * through a temporary DisplayConfig. Mutter ends the session (and removes the display, restoring
 * the physical layout) when this process's bus connection closes, so a crash cannot strand it.
 * Measured on GNOME 50.1 (2026-10-13): the virtual monitor takes the consumer's size and
 * max-framerate (1920x2160@165.000), persists between consumers while the session lives, and DP-1
 * returns on Stop. Virtual monitors offer colour mode 0 only (SDR). */
#pragma once
#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace portal {
class gnome_virtual_t {
  static constexpr const char *sc = "org.gnome.Mutter.ScreenCast";
  static constexpr const char *dc = "org.gnome.Mutter.DisplayConfig";
  static constexpr const char *dc_path = "/org/gnome/Mutter/DisplayConfig";
  GDBusConnection *bus = nullptr;
  std::string session_path;

  static GVariant *call(GDBusConnection *bus, const char *name, const char *path, const char *iface, const char *method,
                        GVariant *args, const char *reply) {
    GError *error = nullptr;
    auto result = g_dbus_connection_call_sync(bus, name, path, iface, method, args,
      reply ? G_VARIANT_TYPE(reply) : nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (!result) {
      BOOST_LOG(debug) << "GNOME " << method << ": " << (error ? error->message : "failed");
      if (error) g_error_free(error);
    }
    return result;
  }

  struct monitor_t {
    std::string connector, vendor, mode_id;
    int width = 0, height = 0;
    double refresh = 0;
    unsigned color_mode = 0;
  };
  struct layout_t {
    guint32 serial = 0;
    std::vector<monitor_t> monitors;
    std::set<std::string> enabled;  // connectors in a logical monitor
    guint32 layout_mode = 0;
    bool has_layout_mode = false;
  };
  static bool state(GDBusConnection *bus, layout_t &out) {
    auto reply = call(bus, dc, dc_path, dc, "GetCurrentState", nullptr,
      "(ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})");
    if (!reply) return false;
    GVariantIter *monitors = nullptr, *logical = nullptr;
    GVariant *props = nullptr;
    g_variant_get(reply, "(ua((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})@a{sv})", &out.serial, &monitors, &logical, &props);
    GVariant *monitor;
    while ((monitor = g_variant_iter_next_value(monitors))) {
      const char *connector, *vendor, *product, *serial;
      GVariantIter *modes = nullptr;
      GVariant *monitor_props = nullptr;
      g_variant_get(monitor, "((&s&s&s&s)a(siiddada{sv})@a{sv})", &connector, &vendor, &product, &serial, &modes, &monitor_props);
      monitor_t m;
      m.connector = connector;
      m.vendor = vendor;
      g_variant_lookup(monitor_props, "color-mode", "u", &m.color_mode);
      GVariant *mode;
      while ((mode = g_variant_iter_next_value(modes))) {
        const char *id;
        gint32 w, h;
        gdouble refresh, scale;
        GVariant *scales = nullptr, *mode_props = nullptr;
        g_variant_get(mode, "(&siidd@ad@a{sv})", &id, &w, &h, &refresh, &scale, &scales, &mode_props);
        gboolean current = FALSE;
        g_variant_lookup(mode_props, "is-current", "b", &current);
        if (current) {
          m.mode_id = id;
          m.width = w;
          m.height = h;
          m.refresh = refresh;
        }
        g_variant_unref(scales);
        g_variant_unref(mode_props);
        g_variant_unref(mode);
      }
      g_variant_iter_free(modes);
      g_variant_unref(monitor_props);
      g_variant_unref(monitor);
      out.monitors.push_back(std::move(m));
    }
    GVariant *entry;
    while ((entry = g_variant_iter_next_value(logical))) {
      gint32 x, y;
      gdouble scale;
      guint32 transform;
      gboolean primary;
      GVariantIter *members = nullptr;
      GVariant *logical_props = nullptr;
      g_variant_get(entry, "(iidub@a(ssss)@a{sv})", &x, &y, &scale, &transform, &primary, nullptr, &logical_props);
      GVariant *member_list = g_variant_get_child_value(entry, 5);
      members = g_variant_iter_new(member_list);
      const char *c, *v, *p, *s;
      while (g_variant_iter_next(members, "(&s&s&s&s)", &c, &v, &p, &s)) out.enabled.insert(c);
      g_variant_iter_free(members);
      g_variant_unref(member_list);
      g_variant_unref(logical_props);
      g_variant_unref(entry);
    }
    g_variant_iter_free(monitors);
    g_variant_iter_free(logical);
    out.has_layout_mode = g_variant_lookup(props, "layout-mode", "u", &out.layout_mode);
    g_variant_unref(props);
    g_variant_unref(reply);
    return true;
  }
  static bool is_virtual(const monitor_t &m) { return m.vendor == "MetaVendor"; }

  // Negotiate WxH and max-framerate on the virtual stream; Mutter sizes the monitor from it.
  bool negotiate(int w, int h, int milli_hz) {
    int fd = open_remote();
    if (fd < 0) return false;
    struct sizer_t {
      std::mutex mutex;
      std::condition_variable cv;
      bool done = false, failed = false;
      pw_stream *stream = nullptr;
    } sizer;
    auto loop = pw_thread_loop_new("umbra-gnome-size", nullptr);
    if (!loop) { close(fd); return false; }
    auto context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    auto core = context ? pw_context_connect_fd(context, fd, nullptr, 0) : nullptr;  // takes fd
    if (!context) close(fd);
    bool ok = false;
    if (core) {
      sizer.stream = pw_stream_new(core, "Umbra virtual display", pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
      static const pw_stream_events events {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = [](void *p, pw_stream_state, pw_stream_state state, const char *) {
          auto &s = *static_cast<sizer_t *>(p);
          if (state == PW_STREAM_STATE_ERROR) { std::lock_guard l(s.mutex); s.failed = true; s.cv.notify_all(); }
        },
        .param_changed = [](void *p, uint32_t id, const spa_pod *param) {
          auto &s = *static_cast<sizer_t *>(p);
          if (id != SPA_PARAM_Format || !param) return;
          std::lock_guard l(s.mutex);
          s.done = true;
          s.cv.notify_all();
        },
        .process = [](void *p) {
          auto &s = *static_cast<sizer_t *>(p);
          if (auto b = pw_stream_dequeue_buffer(s.stream)) pw_stream_queue_buffer(s.stream, b);
        },
      };
      spa_hook listener {};
      if (sizer.stream) {
        pw_stream_add_listener(sizer.stream, &listener, &events, &sizer);
        uint8_t storage[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(storage, sizeof storage);
        auto size = SPA_RECTANGLE(uint32_t(w), uint32_t(h));
        auto zero = SPA_FRACTION(0, 1), rate = SPA_FRACTION(uint32_t(milli_hz), 1000u), low = SPA_FRACTION(1, 1);
        const spa_pod *params[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
          SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
          SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
          SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
          SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA),
          SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&size),
          SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&zero),
          SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&rate, &low, &rate)))};
        if (pw_stream_connect(sizer.stream, PW_DIRECTION_INPUT, node,
              static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1) >= 0 &&
            pw_thread_loop_start(loop) >= 0) {
          std::unique_lock l(sizer.mutex);
          ok = sizer.cv.wait_for(l, std::chrono::seconds(3), [&] { return sizer.done || sizer.failed; }) && !sizer.failed;
        }
      }
      // The monitor must show the requested mode before the sizing stream lets go.
      if (ok) ok = wait_mode(w, h, milli_hz);
      pw_thread_loop_stop(loop);
      if (sizer.stream) pw_stream_destroy(sizer.stream);
      pw_core_disconnect(core);
    }
    if (context) pw_context_destroy(context);
    pw_thread_loop_destroy(loop);
    return ok;
  }
  bool wait_mode(int w, int h, int milli_hz) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
      layout_t layout;
      if (state(bus, layout)) {
        for (auto &m : layout.monitors) {
          if (!is_virtual(m) || (!connector.empty() && m.connector != connector) || (connector.empty() && known.count(m.connector))) continue;
          if (m.width == w && m.height == h && std::fabs(m.refresh * 1000 - milli_hz) < 500) {
            connector = m.connector;
            return true;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
  }
  std::set<std::string> known;  // virtual connectors that existed before this session

public:
  uint32_t node = PW_ID_ANY;
  std::string connector;

  ~gnome_virtual_t() {
    if (bus && !session_path.empty()) {
      if (auto r = call(bus, sc, session_path.c_str(), "org.gnome.Mutter.ScreenCast.Session", "Stop", nullptr, nullptr)) g_variant_unref(r);
    }
    if (bus) g_object_unref(bus);
  }

  static bool available() {
    auto bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    bool ok = false;
    if (auto r = call(bus, sc, "/org/gnome/Mutter/ScreenCast", "org.freedesktop.DBus.Properties", "Get",
                      g_variant_new("(ss)", sc, "Version"), "(v)")) {
      GVariant *v;
      g_variant_get(r, "(v)", &v);
      ok = g_variant_is_of_type(v, G_VARIANT_TYPE_INT32) && g_variant_get_int32(v) >= 4;
      g_variant_unref(v);
      g_variant_unref(r);
    }
    layout_t layout;
    ok = ok && state(bus, layout);
    g_object_unref(bus);
    return ok;
  }

  // Any enabled physical monitor currently in HDR (BT.2100) colour mode.
  static bool physical_hdr_active() {
    auto bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    layout_t layout;
    bool hdr = false;
    if (state(bus, layout))
      for (auto &m : layout.monitors) hdr |= !is_virtual(m) && layout.enabled.count(m.connector) && m.color_mode == 1;
    g_object_unref(bus);
    return hdr;
  }

  bool open(int w, int h, int milli_hz) {
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    layout_t before;
    if (!state(bus, before)) return false;
    for (auto &m : before.monitors) if (is_virtual(m)) known.insert(m.connector);
    auto session = call(bus, sc, "/org/gnome/Mutter/ScreenCast", sc, "CreateSession", g_variant_new("(a{sv})", nullptr), "(o)");
    if (!session) return false;
    const char *path;
    g_variant_get(session, "(&o)", &path);
    session_path = path;
    g_variant_unref(session);
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "cursor-mode", g_variant_new_uint32(1));  // embedded
    g_variant_builder_add(&props, "{sv}", "is-platform", g_variant_new_boolean(TRUE));
    auto stream = call(bus, sc, session_path.c_str(), "org.gnome.Mutter.ScreenCast.Session", "RecordVirtual",
                       g_variant_new("(a{sv})", &props), "(o)");
    if (!stream) return false;
    const char *stream_path;
    g_variant_get(stream, "(&o)", &stream_path);
    auto context = g_main_context_new();
    g_main_context_push_thread_default(context);
    auto subscription = g_dbus_connection_signal_subscribe(bus, sc, "org.gnome.Mutter.ScreenCast.Stream", "PipeWireStreamAdded",
      stream_path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *v, gpointer p) {
        g_variant_get(v, "(u)", static_cast<uint32_t *>(p));
      }, &node, nullptr);
    g_variant_unref(stream);
    bool started = false;
    if (auto r = call(bus, sc, session_path.c_str(), "org.gnome.Mutter.ScreenCast.Session", "Start", nullptr, nullptr)) {
      g_variant_unref(r);
      started = true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (started && node == PW_ID_ANY && std::chrono::steady_clock::now() < deadline) {
      if (!g_main_context_iteration(context, FALSE)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    g_dbus_connection_signal_unsubscribe(bus, subscription);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    if (node == PW_ID_ANY) return false;
    return negotiate(w, h, milli_hz);
  }

  // Resize for a resumed session (same Mutter session; a new negotiation resizes the monitor).
  bool resize(int w, int h, int milli_hz) { return negotiate(w, h, milli_hz); }

  // Windows' exclusive layout: only this virtual monitor, at 0,0, primary. Temporary (method 1):
  // never written to monitors.xml; Mutter restores the stored layout when the monitor goes away.
  bool exclusive() {
    layout_t layout;
    if (!state(bus, layout)) return false;
    const monitor_t *self = nullptr;
    for (auto &m : layout.monitors) if (m.connector == connector) self = &m;
    if (!self || self->mode_id.empty()) return false;
    GVariantBuilder logical, members, props, none;
    g_variant_builder_init(&members, G_VARIANT_TYPE("a(ssa{sv})"));
    g_variant_builder_init(&none, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&members, "(ssa{sv})", self->connector.c_str(), self->mode_id.c_str(), &none);
    g_variant_builder_init(&logical, G_VARIANT_TYPE("a(iiduba(ssa{sv}))"));
    g_variant_builder_add(&logical, "(iiduba(ssa{sv}))", 0, 0, 1.0, 0u, TRUE, &members);
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    if (layout.has_layout_mode) g_variant_builder_add(&props, "{sv}", "layout-mode", g_variant_new_uint32(layout.layout_mode));
    auto r = call(bus, dc, dc_path, dc, "ApplyMonitorsConfig",
                  g_variant_new("(uua(iiduba(ssa{sv}))a{sv})", layout.serial, 1u, &logical, &props), nullptr);
    if (!r) return false;
    g_variant_unref(r);
    layout_t after;
    if (!state(bus, after)) return false;
    for (auto &m : after.monitors) if (m.connector != connector && after.enabled.count(m.connector)) return false;
    return after.enabled.count(connector) == 1;
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
}  // namespace portal
