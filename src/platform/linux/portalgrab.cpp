/* SPDX-License-Identifier: GPL-3.0-or-later
 * XDG ScreenCast capture for sandboxed Linux desktops.
 * The portal owns consent and the session. Copy each PipeWire buffer before
 * returning it; an encoder must never retain a pointer into a requeued buffer.
 */
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>
#include "cuda.h"
#include "vaapi.h"
#include "wayland.h"
#include "src/platform/common.h"
#include "src/video.h"
#include "src/globals.h"
#include "portal.h"
#include "gpu-hevc.h"
#ifdef SUNSHINE_BUILD_PORTAL_HDR
#include "portal-color.h"
#include "portal-dmabuf.h"
#endif
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
#include "kde-output.h"
#endif

using namespace std::literals;

#ifdef SUNSHINE_BUILD_KDE_OUTPUT
#include "kde-capture.h"
#endif

namespace portal {
  constexpr auto service = "org.freedesktop.portal.Desktop";
  constexpr auto path = "/org/freedesktop/portal/desktop";
  constexpr auto interface = "org.freedesktop.portal.ScreenCast";

  class session_t {
  public:
    ~session_t() {
      if (bus && !handle.empty()) {
        auto reply = g_dbus_connection_call_sync(bus, service, handle.c_str(),
          "org.freedesktop.portal.Session", "Close", nullptr, nullptr,
          G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (reply) g_variant_unref(reply);
      }
      if (bus) g_object_unref(bus);
    }

    bool open(uint32_t requested_type = 1, int virtual_width = 1920, int virtual_height = 1080) {
      source_type = requested_type;
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
      if (source_type == 4) {
        native = std::make_unique<kde_virtual_t>();
        if (!native->open(virtual_width, virtual_height)) {
          BOOST_LOG(error) << "KDE unattended virtual capture unavailable; check Umbra capture permissions";
          return false;
        }
        node = native->node;
        has_position = true;
        BOOST_LOG(info) << "KDE native virtual capture created without portal approval";
        return true;
      }
#endif
      // Each caller uses its own context: the tray's GLib loop must not receive
      // callbacks whose stack storage belongs to the encoder-probe thread.
      auto context = g_main_context_new();
      g_main_context_push_thread_default(context);
      auto cleanup = util::fail_guard([&] {
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
      });
      bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
      if (!bus) return false;
      const auto version = property("version");
      if (!(property("AvailableSourceTypes") & source_type)) return false;
      auto token_path = platf::appdata() / (source_type == 4 ? "portal-virtual-token" : "portal-monitor-token");
      // Tokens are backend specific. A desktop switch must get fresh consent.
      if (const char *desktop = std::getenv("XDG_CURRENT_DESKTOP")) {
        std::string suffix(desktop);
        for (auto &c : suffix) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        token_path += "." + suffix.substr(0, 80);
      }
      std::string token;
      if (version >= 4) {
        std::ifstream in(token_path);
        char c;
        while (token.size() < 8192 && in.get(c) && c != '\n') token += c;
        if (token.size() == 8192) token.clear();
      }
      GVariantBuilder options;
      g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
      static std::atomic<uint64_t> session_number {0};
      const auto session_token = "umbra_session_" + std::to_string(++session_number);
      g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(session_token.c_str()));
      auto response = request("CreateSession", options, context);
      if (!response) return false;
      const char *session_handle = nullptr;
      if (g_variant_lookup(response, "session_handle", "&s", &session_handle)) handle = session_handle;
      g_variant_unref(response);
      if (handle.empty()) return false;

      g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(source_type));
      g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(false));
      if (property("AvailableCursorModes") & 2)
        g_variant_builder_add(&options, "{sv}", "cursor_mode", g_variant_new_uint32(2));
      if (version >= 4) {
        g_variant_builder_add(&options, "{sv}", "persist_mode", g_variant_new_uint32(2));
        if (!token.empty())
          g_variant_builder_add(&options, "{sv}", "restore_token", g_variant_new_string(token.c_str()));
      }
      response = request("SelectSources", options, context);
      if (!response) return false;
      g_variant_unref(response);
      // A restore token is single-use. Keep no stale token after trying Start.
      if (!token.empty()) {
        std::error_code ec;
        std::filesystem::remove(token_path, ec);
      }
      BOOST_LOG(info) << "Portal capture: approve screen sharing in your desktop if prompted.";
      g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
      response = request("Start", options, context);
      if (!response) return false;
      auto response_cleanup = util::fail_guard([&] { g_variant_unref(response); });
      auto streams = g_variant_lookup_value(response, "streams", G_VARIANT_TYPE("a(ua{sv})"));
      if (!streams) return false;
      auto streams_cleanup = util::fail_guard([&] { g_variant_unref(streams); });
      if (g_variant_n_children(streams) != 1) return false;
      GVariant *properties;
      g_variant_get_child(streams, 0, "(u@a{sv})", &node, &properties);
      uint32_t returned_type = 0;
      g_variant_lookup(properties, "source_type", "u", &returned_type);
      if (source_type == 4 && returned_type != 4) {
        g_variant_unref(properties);
        return false;
      }
      has_position = g_variant_lookup(properties, "position", "(ii)", &x, &y);
      g_variant_lookup(properties, "size", "(ii)", &width, &height);
      g_variant_unref(properties);
      const char *replacement = nullptr;
      if (g_variant_lookup(response, "restore_token", "&s", &replacement) && replacement && *replacement) {
        GError *error = nullptr;
        if (!g_file_set_contents_full(token_path.c_str(), replacement, -1,
              G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error)) {
          BOOST_LOG(warning) << "Could not save portal permission: " << error->message;
          g_error_free(error);
        }
      }
      return node != PW_ID_ANY;
    }

    // Each PipeWire core needs its own connection. dup() shares the same socket
    // protocol state and cannot reconnect after an encoder probe destroys its core.
    std::string virtual_output_name() const {
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
      if (native) return native->output_name;
#endif
      return {};
    }
    int open_remote() {
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
      if (native) return native->open_remote();
#endif
      GVariantBuilder options;
      g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
      GUnixFDList *fds = nullptr;
      auto reply = g_dbus_connection_call_with_unix_fd_list_sync(bus, service, path, interface,
        "OpenPipeWireRemote", g_variant_new("(oa{sv})", handle.c_str(), &options), G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &fds, nullptr, nullptr);
      if (!reply) return -1;
      int index;
      g_variant_get(reply, "(h)", &index);
      int fd = fds ? g_unix_fd_list_get(fds, index, nullptr) : -1;
      if (fds) g_object_unref(fds);
      g_variant_unref(reply);
      return fd;
    }

    int x = 0, y = 0, width = 0, height = 0;
    bool has_position = false;
    uint32_t node = PW_ID_ANY, source_type = 1;

  private:
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
    std::unique_ptr<kde_virtual_t> native;
#endif
    GDBusConnection *bus = nullptr;
    std::string handle;
    unsigned request_number = 0;

    uint32_t property(const char *name) {
      auto reply = g_dbus_connection_call_sync(bus, service, path, "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", interface, name), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
      if (!reply) return 0;
      GVariant *value;
      g_variant_get(reply, "(v)", &value);
      auto result = g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) ? g_variant_get_uint32(value) : 0;
      g_variant_unref(value);
      g_variant_unref(reply);
      return result;
    }

    GVariant *request(const char *method, GVariantBuilder &options, GMainContext *context) {
      struct wait_t {
        GVariant *response = nullptr;
        bool expired = false;
        std::chrono::steady_clock::time_point deadline;
      } wait;
      wait.deadline = std::chrono::steady_clock::now() + (std::strcmp(method, "Start") == 0 ? 120s : 15s);
      std::string sender = g_dbus_connection_get_unique_name(bus) + 1;
      std::replace(sender.begin(), sender.end(), '.', '_');
      auto token = "umbra_" + std::to_string(++request_number);
      auto request_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
      g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
      auto subscription = g_dbus_connection_signal_subscribe(bus, service,
        "org.freedesktop.portal.Request", "Response", request_path.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *v, gpointer p) {
          auto &w = *static_cast<wait_t *>(p);
          if (!w.response) w.response = g_variant_ref(v);
        }, &wait, nullptr);
      auto timer = g_timeout_source_new(100);
      g_source_set_callback(timer, [](gpointer p) -> gboolean {
        auto &wait = *static_cast<wait_t *>(p);
        wait.expired = std::chrono::steady_clock::now() >= wait.deadline ||
          (mail::man && mail::man->event<bool>(mail::shutdown)->peek());
        return wait.expired ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
      }, &wait, nullptr);
      g_source_attach(timer, context);
      auto cleanup = util::fail_guard([&] {
        g_source_destroy(timer);
        g_source_unref(timer);
        g_dbus_connection_signal_unsubscribe(bus, subscription);
        if (wait.response) g_variant_unref(wait.response);
      });
      GVariant *args = handle.empty() ? g_variant_new("(a{sv})", &options) :
        std::strcmp(method, "Start") == 0 ? g_variant_new("(osa{sv})", handle.c_str(), "", &options) :
        g_variant_new("(oa{sv})", handle.c_str(), &options);
      GError *error = nullptr;
      auto reply = g_dbus_connection_call_sync(bus, service, path, interface, method, args,
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
      if (!reply) {
        BOOST_LOG(warning) << "Portal " << method << ": " << error->message;
        g_error_free(error);
        return nullptr;
      }
      g_variant_unref(reply);
      while (!wait.response && !wait.expired) g_main_context_iteration(context, true);
      if (!wait.response) {
        auto closed = g_dbus_connection_call_sync(bus, service, request_path.c_str(),
          "org.freedesktop.portal.Request", "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (closed) g_variant_unref(closed);
        BOOST_LOG(warning) << "Portal " << method << " timed out.";
        return nullptr;
      }
      uint32_t code;
      GVariant *result;
      g_variant_get(wait.response, "(u@a{sv})", &code, &result);
      if (code) {
        BOOST_LOG(warning) << "Portal " << method << " cancelled or rejected (" << code << ").";
        g_variant_unref(result);
        return nullptr;
      }
      return result;
    }
  };

  std::mutex session_mutex;
  std::shared_ptr<session_t> active_session;
  std::chrono::steady_clock::time_point retry_after;
  std::shared_ptr<session_t> get_session() {
    std::lock_guard lock(session_mutex);
    if (!active_session) {
      if (std::chrono::steady_clock::now() < retry_after) return nullptr;
      auto candidate = std::make_shared<session_t>();
      if (!candidate->open()) {
        // Encoder enumeration tries several devices; cancellation must not
        // immediately present the same consent dialog for each one.
        retry_after = std::chrono::steady_clock::now() + 30s;
        return nullptr;
      }
      active_session = std::move(candidate);
    }
    return active_session;
  }

  struct image_t: gpu_hevc::cpu_image_t {
    std::vector<uint8_t> pixels;
  };

  class display_t: public platf::display_t {
  public:
    ~display_t() override {
      if (loop) pw_thread_loop_stop(loop);
      streaming = false;
      // A failed attempt never tears down the stream's virtual display (owned by launch/Quit) or a
      // session the CPU-frame retry is about to reuse.
      if (!initialized && !retry_sdr && !gpu_attempt && session && session->source_type != 4) {
        std::lock_guard lock(session_mutex);
        if (active_session == session) active_session.reset();
      }
      if (stream) pw_stream_destroy(stream);
      if (core) pw_core_disconnect(core);
      if (context) pw_context_destroy(context);
      if (loop) pw_thread_loop_destroy(loop);
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      if (color) umbra_portal_color_close(color);
#endif
    }

    // gpu_frames_allowed: a native GPU encoder (NVENC/VA-API) will consume this capture, so frames
    // may stay in GPU memory as on Windows. Never for GPU HEVC sessions, which take CPU pixels.
    // readback_allowed: GPU HEVC session; take DMA-BUF and read it back here instead of the compositor's MemFd copy.
    bool init(platf::mem_type_e type, bool allow_hdr, bool gpu_frames_allowed, bool readback_allowed = false,
              int probe_width = 1920, int probe_height = 1080) {
      retry_sdr = allow_hdr;
      mem_type = type;
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
      // Where every stream gets its own native virtual display, a display requested while none
      // exists is the encoder probe (startup, or after a stream): it only encodes blank test
      // images, so it needs no capture and must never open the desktop's screen-sharing prompt.
      bool idle;
      {
        std::lock_guard lock(session_mutex);
        idle = !active_session;
      }
      if (idle && platf::portal_coop_supported()) {
        probe = true;
        width = negotiated_width = probe_width > 0 ? probe_width : 1920;
        height = negotiated_height = probe_height > 0 ? probe_height : 1080;
        latest.assign(size_t(width) * height * 4, 0);
        initialized = true;
        BOOST_LOG(info) << "Portal encoder probe display " << width << 'x' << height << " (no capture, no prompt)";
        return true;
      }
#endif
      session = get_session();
      if (!session) return false;
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      if (allow_hdr && session->has_position) {
        umbra_portal_hdr output {};
        color = umbra_portal_color_open(session->x, session->y, &output);
        if (color && output.hdr) {
          dma = std::make_unique<dma_reader_t>();
          if (!dma->init()) { dma.reset(); umbra_portal_color_close(color); color = nullptr; }
          else {
            for (unsigned i=0;i<3;++i) {
              hdr_metadata.displayPrimaries[i].x = (output.xy[i*2]+10)/20;
              hdr_metadata.displayPrimaries[i].y = (output.xy[i*2+1]+10)/20;
            }
            hdr_metadata.whitePoint.x = (output.xy[6]+10)/20;
            hdr_metadata.whitePoint.y = (output.xy[7]+10)/20;
            hdr_metadata.minDisplayLuminance = output.min_luminance;
            hdr_metadata.maxDisplayLuminance = output.max_luminance;
            hdr_metadata.maxContentLightLevel = output.max_cll;
            hdr_metadata.maxFrameAverageLightLevel = output.max_fall;
          }
        }
        if (!color) BOOST_LOG(info) << "Portal HDR output metadata/import unavailable; using SDR capture";
      }
#endif
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      // Offer DMA-BUF only in modifiers this GPU's EGL can import, and only to an encoder that can
      // take it: VA-API always, CUDA when the compositor's GPU is NVIDIA (Vibepollo's pipewire
      // policy). CPU buffers stay offered after it as the fallback.
      // GPU HEVC sessions take CPU pixels, but the compositor's own GPU-to-memory copy (MemFd) is
      // slow and stalls its compositing (measured ~39 fps at 1920x2176 on KWin). They also take
      // DMA-BUF and read it back here, on this capture's thread. Vendor-neutral: EGL + GL only.
      const bool want_gpu_frames = gpu_frames_allowed && (type == platf::mem_type_e::vaapi || type == platf::mem_type_e::cuda);
      const bool want_readback = readback_allowed && !gpu_frames_allowed;
      if (!dma && (want_gpu_frames || want_readback)) {
        auto reader = std::make_unique<dma_reader_t>();
        if (reader->init(DRM_FORMAT_XRGB8888) &&
            (want_readback || type == platf::mem_type_e::vaapi || reader->vendor.find("NVIDIA") != std::string::npos)) {
          sdr_dma = std::move(reader);
          readback = want_readback;
          gpu_attempt = true;
        } else {
          BOOST_LOG(info) << "Portal GPU frames unavailable for this encoder; using CPU frames";
        }
      }
#endif
      static std::once_flag once;
      std::call_once(once, [] { pw_init(nullptr, nullptr); });
      loop = pw_thread_loop_new("umbra-portal", nullptr);
      if (!loop) return false;
      context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
      if (!context) return false;
      int fd = session->open_remote();
      if (fd < 0) return false;
      core = pw_context_connect_fd(context, fd, nullptr, 0); // PipeWire takes fd ownership, including failure.
      if (!core) return false;
      stream = pw_stream_new(core, "Umbra screen capture", pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
      if (!stream) return false;
      pw_stream_add_listener(stream, &listener, &events, this);
      uint8_t storage[8192];
      spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof storage);
      std::vector<const spa_pod *> params;
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      if (dma) {
        spa_pod_frame object, choice;
        spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
        spa_pod_builder_add(&builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
          SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
          SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_xBGR_210LE),
          SPA_FORMAT_VIDEO_colorPrimaries, SPA_POD_Id(SPA_VIDEO_COLOR_PRIMARIES_BT2020),
          SPA_FORMAT_VIDEO_transferFunction, SPA_POD_Id(14u /* SMPTE2084, PipeWire1.6 */), 0);
        spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(&builder, &choice, SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(&builder, dma->modifiers.front());
        for (auto modifier : dma->modifiers) spa_pod_builder_long(&builder, modifier);
        spa_pod_builder_pop(&builder, &choice);
        params.push_back(static_cast<const spa_pod *>(spa_pod_builder_pop(&builder, &object)));
      }
      if (sdr_dma) {
        spa_pod_frame object, choice;
        spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
        spa_pod_builder_add(&builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
          SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
          SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA), 0);
        spa_pod_builder_prop(&builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(&builder, &choice, SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(&builder, sdr_dma->modifiers.front());
        for (auto modifier : sdr_dma->modifiers) spa_pod_builder_long(&builder, modifier);
        spa_pod_builder_pop(&builder, &choice);
        params.push_back(static_cast<const spa_pod *>(spa_pod_builder_pop(&builder, &object)));
      }
#endif
      params.push_back(static_cast<const spa_pod *>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA))));
      if (pw_stream_connect(stream, PW_DIRECTION_INPUT, session->node,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params.data(), params.size()) < 0 ||
          pw_thread_loop_start(loop) < 0) {
        BOOST_LOG(warning) << "Portal capture: PipeWire stream connect failed";
        return false;
      }
      std::unique_lock lock(mutex);
      if (!changed.wait_for(lock, 5s, [&] { return failed || (sequence && (session->source_type != 4 ||
            (frame_width == session->width && frame_height == session->height))); }) || failed) {
        BOOST_LOG(warning) << "Portal capture: " << (failed ? "stream failed" : "no frame within 5s") << " ("
          << (sdr_dma ? "GPU frames offered" : "CPU frames") << ", frames=" << sequence << ')';
        return false;
      }
      hdr = frame_hdr;
      gpu_frames = negotiated_gpu;
      format_modifier = (format.flags & SPA_VIDEO_FLAG_MODIFIER) != 0;
      width = frame_width;
      height = frame_height;
      offset_x = session->x;
      offset_y = session->y;
      env_width = offset_x + width;
      env_height = offset_y + height;
      for (auto &monitor : wl::monitors()) {
        env_width = std::max(env_width, monitor->viewport.offset_x + monitor->viewport.width);
        env_height = std::max(env_height, monitor->viewport.offset_y + monitor->viewport.height);
      }
      initialized = true;
      BOOST_LOG(info) << "Portal capture ready: " << width << 'x' << height << (hdr ? " (PipeWire DMA-BUF HDR/PQ)" :
        gpu_frames ? " (PipeWire DMA-BUF SDR, GPU frames)" : readback && format_modifier ? " (PipeWire DMA-BUF SDR, read back)" :
        " (PipeWire SDR buffers)");
      return true;
    }

    std::shared_ptr<platf::img_t> alloc_img() override {
      if (gpu_frames) {
        auto img = std::make_shared<egl::img_descriptor_t>();
        img->width = width;
        img->height = height;
        img->serial = std::numeric_limits<decltype(img->serial)>::max();
        img->data = nullptr;
        img->pixel_pitch = 4;
        img->sequence = 0;
        std::fill_n(img->sd.fds, 4, -1);
        return img;
      }
      auto img = std::make_shared<image_t>();
      img->capture_format = capture_format();
      img->width = width;
      img->height = height;
      img->pixel_pitch = 4;
      img->row_pitch = width * 4;
      img->pixels.resize(static_cast<size_t>(img->row_pitch) * height);
      img->data = img->pixels.data();
      return img;
    }

    int dummy_img(platf::img_t *img) override {
      if (gpu_frames) return 0;  // sequence 0 marks a dummy for the VRAM encoders
      std::lock_guard lock(mutex);
      if (negotiated_width != width || negotiated_height != height || negotiated_hdr != hdr || latest.empty()) return -1;
      std::memcpy(img->data, latest.data(), latest.size());
      img->frame_timestamp = timestamp;
      return 0;
    }

    std::unique_ptr<platf::avcodec_encode_device_t> make_avcodec_encode_device(platf::pix_fmt_e) override {
      if (gpu_frames) {
#ifdef SUNSHINE_BUILD_VAAPI
        if (mem_type == platf::mem_type_e::vaapi) return va::make_avcodec_encode_device(width, height, true);
#endif
#ifdef SUNSHINE_BUILD_CUDA
        if (mem_type == platf::mem_type_e::cuda) return cuda::make_avcodec_gl_encode_device(width, height, 0, 0);
#endif
        return nullptr;
      }
#ifdef SUNSHINE_BUILD_VAAPI
      if (mem_type == platf::mem_type_e::vaapi) return va::make_avcodec_encode_device(width, height, false);
#endif
#ifdef SUNSHINE_BUILD_CUDA
      if (mem_type == platf::mem_type_e::cuda) return cuda::make_avcodec_encode_device(width, height, false);
#endif
      return std::make_unique<platf::avcodec_encode_device_t>();
    }

    std::unique_ptr<platf::gpu_hevc_encode_device_t> make_gpu_hevc_encode_device(const video::config_t &config) override {
      if (gpu_frames) return nullptr;  // GPU HEVC reads CPU pixels; portal_display never offers it GPU frames
      return gpu_hevc::make_device(config, is_hdr(), capture_format());
    }

    bool is_hdr() override { return hdr; }
    bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      if (!hdr) return false;
      metadata = hdr_metadata;
      return true;
    }

    platf::capture_e capture(const push_captured_image_cb_t &push, const pull_free_image_cb_t &pull, bool *) override {
      if (probe) return platf::capture_e::error;  // probe displays only serve dummy images
      uint64_t consumed = 0, delivered = 0, stat_sequence = 0;
      auto stat_start = std::chrono::steady_clock::now();
      // Every 10s: frames the compositor delivered vs frames handed to the encoder (pacing evidence).
      auto report = [&](uint64_t now_sequence) {
        const auto now = std::chrono::steady_clock::now();
        if (!stat_sequence) {  // the window starts at the first delivered frame
          stat_sequence = now_sequence;
          delivered = 0;
          stat_start = now;
          return;
        }
        const auto seconds = std::chrono::duration<double>(now - stat_start).count();
        if (seconds < 10) return;
        BOOST_LOG(info) << "[UMBRA-CAPTURE] " << width << 'x' << height << (gpu_frames ? " gpu" : " cpu")
          << " arrived=" << (now_sequence - stat_sequence) << " delivered=" << delivered << " in "
          << std::fixed << std::setprecision(1) << seconds << 's';
        stat_sequence = now_sequence;
        delivered = 0;
        stat_start = now;
      };
      for (;;) {
#ifdef SUNSHINE_BUILD_PORTAL_HDR
        if (color && !umbra_portal_color_current(color)) return platf::capture_e::reinit;
#endif
        std::unique_lock lock(mutex);
        changed.wait_for(lock, 100ms, [&] { return failed || sequence != consumed; });
        if (failed) {
          lock.unlock();
          std::lock_guard session_lock(session_mutex);
          if (active_session == session) active_session.reset();
          return platf::capture_e::error;
        }
        if (negotiated_width != width || negotiated_height != height || negotiated_hdr != hdr) return platf::capture_e::reinit;
        if (consumed == sequence) {
          lock.unlock();
          if (!push(nullptr, false)) return platf::capture_e::ok;
          continue;
        }
        // Never hold the PipeWire callback mutex while waiting for an encoder image.
        lock.unlock();
        std::shared_ptr<platf::img_t> img;
        if (!pull(img)) return platf::capture_e::interrupted;
        lock.lock();
        if (negotiated_width != width || negotiated_height != height || negotiated_hdr != hdr ||
            negotiated_gpu != gpu_frames) return platf::capture_e::reinit;
        if (gpu_frames) {
          if (held_sd.fds[0] < 0) return platf::capture_e::reinit;  // buffers renegotiated
          auto descriptor = static_cast<egl::img_descriptor_t *>(img.get());
          descriptor->reset();
          descriptor->sd = held_sd;
          bool ok = true;
          for (int i = 0; i < 4; ++i) {
            descriptor->sd.fds[i] = held_sd.fds[i] < 0 ? -1 : dup(held_sd.fds[i]);
            if (held_sd.fds[i] >= 0 && descriptor->sd.fds[i] < 0) ok = false;
          }
          if (!ok) { descriptor->reset(); return platf::capture_e::error; }
          descriptor->sequence = sequence;
          descriptor->data = nullptr;
        } else {
          std::memcpy(img->data, latest.data(), latest.size());
        }
        img->frame_timestamp = timestamp;
        consumed = sequence;
        lock.unlock();
        ++delivered;
        report(consumed);
        if (!push(std::move(img), true)) return platf::capture_e::ok;
      }
    }

  private:
    gpu_hevc::capture_format_e capture_format() const {
      return hdr ? gpu_hevc::capture_format_e::bt2020_pq_xbgr2101010 : gpu_hevc::capture_format_e::bgra8;
    }
    std::shared_ptr<session_t> session;
    platf::mem_type_e mem_type;
    bool hdr = false, negotiated_hdr = false, frame_hdr = false, retry_sdr = false;
    // GPU frames: the newest DMA-BUF stays checked out (held) until a newer one replaces it; the
    // encoder imports duplicated fds. Read under mutex; PipeWire thread owns requeueing.
    bool gpu_frames = false, negotiated_gpu = false, gpu_attempt = false, probe = false, readback = false, format_modifier = false;
    pw_buffer *held = nullptr;
    egl::surface_descriptor_t held_sd {0, 0, {-1, -1, -1, -1}, 0, 0, {}, {}};
    SS_HDR_METADATA hdr_metadata {};
#ifdef SUNSHINE_BUILD_PORTAL_HDR
    umbra_portal_color *color = nullptr;
    std::unique_ptr<dma_reader_t> dma, sdr_dma;
#endif
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener {};
    spa_video_info_raw format {};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<uint8_t> latest, pending;
    int negotiated_width = 0, negotiated_height = 0;
    int frame_width = 0, frame_height = 0;
    uint64_t sequence = 0;
    bool failed = false, streaming = false, initialized = false;
    std::chrono::steady_clock::time_point timestamp;

    void fail(const char *why = nullptr) {
      if (why) BOOST_LOG(warning) << "Portal capture failed: " << why;
      std::lock_guard lock(mutex);
      failed = true;
      changed.notify_all();
    }
    static void state_changed(void *data, pw_stream_state, pw_stream_state state, const char *error) {
      auto &self = *static_cast<display_t *>(data);
      if (state == PW_STREAM_STATE_ERROR || (self.streaming && state == PW_STREAM_STATE_UNCONNECTED)) {
        BOOST_LOG(warning) << "Portal PipeWire stream ended: " << (error ? error : "disconnected");
        self.fail();
      }
      if (state == PW_STREAM_STATE_STREAMING) self.streaming = true;
    }
    static void param_changed(void *data, uint32_t id, const spa_pod *param) {
      auto &self = *static_cast<display_t *>(data);
      if (id != SPA_PARAM_Format || !param) return;
      if (spa_format_video_raw_parse(param, &self.format) < 0) { self.fail(); return; }
      const auto w = self.format.size.width, h = self.format.size.height;
      const bool pq = self.format.format == SPA_VIDEO_FORMAT_xBGR_210LE;
      bool allow_pq = false;
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      allow_pq = self.dma && self.color && (self.format.flags & SPA_VIDEO_FLAG_MODIFIER) &&
        self.format.color_primaries == SPA_VIDEO_COLOR_PRIMARIES_BT2020 && self.format.transfer_function == 14u;
#endif
      bool gpu = false;  // DMA-BUF negotiated (GPU frames, or read back for GPU HEVC)
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      gpu = !pq && self.sdr_dma && (self.format.flags & SPA_VIDEO_FLAG_MODIFIER) &&
        std::find(self.sdr_dma->modifiers.begin(), self.sdr_dma->modifiers.end(), self.format.modifier) != self.sdr_dma->modifiers.end();
#endif
      if (!w || !h || w > 16384 || h > 16384 || uint64_t(w) * h * 4 > 256 * 1024 * 1024 ||
          (pq ? !allow_pq : (self.format.format != SPA_VIDEO_FORMAT_BGRA && self.format.format != SPA_VIDEO_FORMAT_BGRx)) ||
          (!pq && (self.format.flags & SPA_VIDEO_FLAG_MODIFIER) && !gpu)) {
        BOOST_LOG(warning) << "Portal format rejected: " << w << 'x' << h << " spa_format=" << self.format.format
          << " modifier_flag=" << ((self.format.flags & SPA_VIDEO_FLAG_MODIFIER) ? 1 : 0)
          << " modifier=0x" << std::hex << self.format.modifier << std::dec << " gpu_offered=" << (self.sdr_dma ? 1 : 0);
        self.fail(); return;
      }
      {
        std::lock_guard lock(self.mutex);
        self.negotiated_gpu = gpu && !self.readback;
        self.negotiated_hdr = pq;
        self.negotiated_width = w;
        self.negotiated_height = h;
        self.changed.notify_all();
      }
      uint8_t storage[1024];
      spa_pod_builder b = SPA_POD_BUILDER_INIT(storage, sizeof storage);
      const spa_pod *params[] = {
        static_cast<const spa_pod *>(spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
          SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(gpu ? 4 : 3, 2, 4),  // GPU frames keep one checked out
          SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size, SPA_POD_Int(w * h * 4),
          SPA_PARAM_BUFFERS_stride, SPA_POD_Int(w * 4), SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << (pq || gpu ? SPA_DATA_DmaBuf : SPA_DATA_MemFd)))),
        static_cast<const spa_pod *>(spa_pod_builder_add_object(&b, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
          SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header), SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))))};
      if (pw_stream_update_params(self.stream, params, 2) < 0) self.fail("buffer parameters rejected");
    }
    static void process(void *data) {
      auto &self = *static_cast<display_t *>(data);
      pw_buffer *buffer = nullptr;
      while (auto next = pw_stream_dequeue_buffer(self.stream)) {
        if (buffer) pw_stream_queue_buffer(self.stream, buffer);
        buffer = next;
      }
      if (!buffer) return;
      auto requeue = util::fail_guard([&] { pw_stream_queue_buffer(self.stream, buffer); });
      auto &b = *buffer->buffer;
      if (!b.n_datas) return;
      auto &d = b.datas[0];
      auto header = static_cast<spa_meta_header *>(spa_buffer_find_meta_data(&b, SPA_META_Header, sizeof(spa_meta_header)));
      if (!d.chunk || (d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) ||
          (header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED))) return;
      const auto w = self.format.size.width, h = self.format.size.height;
      const bool pq = self.format.format == SPA_VIDEO_FORMAT_xBGR_210LE;
      if (!w || !h) { self.fail(); return; }
#ifdef SUNSHINE_BUILD_PORTAL_HDR
      if (!pq && (self.format.flags & SPA_VIDEO_FLAG_MODIFIER) && !self.readback) {
        // GPU frame: no copy. Keep this buffer checked out; requeue the one it replaces.
        if (b.n_datas > 4) { self.fail("GPU frame has more than 4 planes"); return; }
        egl::surface_descriptor_t sd {int(w), int(h), {-1, -1, -1, -1},
          self.format.format == SPA_VIDEO_FORMAT_BGRA ? uint32_t(DRM_FORMAT_ARGB8888) : uint32_t(DRM_FORMAT_XRGB8888),
          self.format.modifier, {}, {}};
        for (uint32_t i = 0; i < b.n_datas; ++i) {
          const auto &plane = b.datas[i];
          if (plane.type != SPA_DATA_DmaBuf || plane.fd < 0 || !plane.chunk || plane.chunk->stride <= 0) {
            BOOST_LOG(warning) << "Portal GPU frame plane " << i << " unusable: type=" << plane.type << " fd=" << plane.fd
              << " stride=" << (plane.chunk ? plane.chunk->stride : -1);
            self.fail(); return;
          }
          sd.fds[i] = int(plane.fd);
          sd.pitches[i] = uint32_t(plane.chunk->stride);
          sd.offsets[i] = plane.chunk->offset;
        }
        pw_buffer *previous;
        {
          std::lock_guard lock(self.mutex);
          previous = self.held;
          self.held = buffer;
          self.held_sd = sd;
          self.frame_hdr = false;
          self.frame_width = w;
          self.frame_height = h;
          self.timestamp = std::chrono::steady_clock::now();
          ++self.sequence;
          self.changed.notify_all();
        }
        requeue.disable();
        if (previous) pw_stream_queue_buffer(self.stream, previous);
        return;
      }
#endif
      auto &pixels = self.pending;
      try {
        if (pq) {
#ifdef SUNSHINE_BUILD_PORTAL_HDR
          if (!self.dma || !self.dma->read(b, self.format, pixels)) { self.fail(); return; }
#else
          self.fail(); return;
#endif
        } else if (self.readback && (self.format.flags & SPA_VIDEO_FLAG_MODIFIER)) {
#ifdef SUNSHINE_BUILD_PORTAL_HDR
          if (!self.sdr_dma->read(b, self.format, pixels)) { self.fail("GPU frame read-back failed"); return; }
#endif
        } else {
          if (!d.data || d.type != SPA_DATA_MemFd || !d.chunk->size || d.chunk->stride < static_cast<int>(w * 4) ||
              d.chunk->offset > d.maxsize || uint64_t(h - 1) * d.chunk->stride + w * 4 > d.maxsize - d.chunk->offset ||
              uint64_t(h - 1) * d.chunk->stride + w * 4 > d.chunk->size) { self.fail(); return; }
          pixels.resize(static_cast<size_t>(w) * h * 4);
          for (uint32_t row = 0; row < h; ++row)
            std::memcpy(pixels.data() + size_t(row) * w * 4,
              static_cast<uint8_t *>(d.data) + d.chunk->offset + size_t(row) * d.chunk->stride, w * 4);
        }
      } catch (const std::exception &failure) {
        BOOST_LOG(error) << "Portal capture allocation/import failed: " << failure.what();
        self.fail(); return;
      }
      std::lock_guard lock(self.mutex);
      self.latest.swap(pixels);
      self.frame_hdr = pq;
      // Arrival time shares the host steady clock. Do not mix compositor PTS
      // into latency accounting without verifying its clock mapping.
      self.frame_width = w;
      self.frame_height = h;
      self.timestamp = std::chrono::steady_clock::now();
      ++self.sequence;
      self.changed.notify_all();
    }
    static void remove_buffer(void *data, pw_buffer *buffer) {
      auto &self = *static_cast<display_t *>(data);
      std::lock_guard lock(self.mutex);
      if (self.held == buffer) {
        self.held = nullptr;
        std::fill_n(self.held_sd.fds, 4, -1);
      }
    }
    static constexpr pw_stream_events events {
      .version = PW_VERSION_STREAM_EVENTS,
      .state_changed = state_changed,
      .param_changed = param_changed,
      .remove_buffer = remove_buffer,
      .process = process,
    };
  };
} // namespace portal

namespace platf {
  namespace {
    std::mutex inhibit_mutex;
    GDBusConnection *inhibit_bus = nullptr;
    std::string inhibit_handle;
  }

  void portal_inhibit(bool active) {
    std::lock_guard lock(inhibit_mutex);
    if (active && !inhibit_bus) {
      inhibit_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
      if (!inhibit_bus) return;
      GVariantBuilder options;
      g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add(&options, "{sv}", "reason", g_variant_new_string("Umbra is streaming a game"));
      GError *error = nullptr;
      auto reply = g_dbus_connection_call_sync(inhibit_bus, portal::service, portal::path,
        "org.freedesktop.portal.Inhibit", "Inhibit", g_variant_new("(sua{sv})", "", 12u, &options),
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
      if (reply) {
        const char *handle;
        g_variant_get(reply, "(&o)", &handle);
        inhibit_handle = handle;
        g_variant_unref(reply);
      } else {
        BOOST_LOG(warning) << "Desktop idle inhibition unavailable: " << error->message;
        g_error_free(error);
        g_clear_object(&inhibit_bus);
      }
    } else if (!active && inhibit_bus) {
      if (!inhibit_handle.empty()) {
        auto reply = g_dbus_connection_call_sync(inhibit_bus, portal::service, inhibit_handle.c_str(),
          "org.freedesktop.portal.Request", "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (reply) g_variant_unref(reply);
        inhibit_handle.clear();
      }
      g_clear_object(&inhibit_bus);
    }
  }

  bool portal_virtual_active() {
    std::lock_guard lock(portal::session_mutex);
    return portal::active_session && portal::active_session->source_type == 4;
  }

#ifdef SUNSHINE_BUILD_KDE_OUTPUT
  // Outputs the exclusive layout switched off, kept until they are back on (survives a crash).
  static std::string exclusive_state_path() {
    return (appdata() / "kde_exclusive_outputs").string();
  }
#endif

  void portal_restore_outputs() {
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
    const auto path = exclusive_state_path();
    if (!std::filesystem::exists(path)) return;
    if (umbra_kde_output_restore(path.c_str())) BOOST_LOG(info) << "Restored the displays the virtual display replaced";
    else BOOST_LOG(error) << "Could not re-enable every display listed in " << path;
#endif
  }

  void portal_release_virtual() {
    {
      std::lock_guard lock(portal::session_mutex);
      if (!portal::active_session || portal::active_session->source_type != 4) return;
      portal::active_session.reset();
    }
    BOOST_LOG(info) << "Released the stream's virtual display";
    portal_restore_outputs();
  }

  bool portal_prepare_virtual(int width, int height, int milli_hz) {
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
    if (!portal_coop_supported() || width < 64 || width > 8192 || height < 64 || height > 8192 ||
        width % 2 || height % 2 || milli_hz < 1000 || milli_hz > 360000) return false;
    std::lock_guard lock(portal::session_mutex);
    auto session = portal::active_session;
    if (!session || session->source_type != 4) {
      session = std::make_shared<portal::session_t>();
      if (!session->open(4, width, height)) return false;
    }
    const char *app_id = std::getenv("FLATPAK_ID");
    if (!app_id) return false;
    std::string output = session->virtual_output_name();
    if (!umbra_kde_output_resize(output.c_str(), width, height, milli_hz, &session->x, &session->y)) return false;
    // Windows Umbra's default layout (virtual_display_layout=exclusive): the stream's virtual
    // display is the only active screen, so games open on it at the client's size and rate.
    if (umbra_kde_output_exclusive(output.c_str(), exclusive_state_path().c_str())) {
      session->x = 0;
      session->y = 0;
      BOOST_LOG(info) << "Virtual display " << output << " is the only active display";
    } else {
      BOOST_LOG(error) << "Could not make the virtual display the only active display; other displays stay on";
    }
    session->width = width;
    session->height = height;
    portal::active_session = std::move(session);
    BOOST_LOG(info) << "Portal virtual pane verified: " << width << 'x' << height << " @ " << milli_hz << " mHz";
    return true;
#else
    return false;
#endif
  }

  bool portal_coop_supported() {
#ifdef SUNSHINE_BUILD_KDE_OUTPUT
    portal::kde_virtual_t native;
    return native.available() && umbra_kde_output_supported();
#else
    return false;
#endif
  }

  bool portal_available() {
    auto bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    auto reply = g_dbus_connection_call_sync(bus, portal::service, portal::path, "org.freedesktop.DBus.Properties",
      "Get", g_variant_new("(ss)", portal::interface, "AvailableSourceTypes"), G_VARIANT_TYPE("(v)"),
      G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
    g_object_unref(bus);
    if (!reply) return false;
    GVariant *value;
    g_variant_get(reply, "(v)", &value);
    bool available = g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) && (g_variant_get_uint32(value) & 1);
    g_variant_unref(value);
    g_variant_unref(reply);
    return available;
  }
  void portal_shutdown() {
    portal_inhibit(false);
    std::lock_guard lock(portal::session_mutex);
    portal::active_session.reset();
    portal::retry_after = {};
  }
  std::vector<std::string> portal_display_names() { return {"portal"}; }
  std::shared_ptr<display_t> portal_display(mem_type_e type, const std::string &name, const video::config_t &config) {
    if ((!name.empty() && name != "portal") ||
        (type != mem_type_e::system && type != mem_type_e::cuda && type != mem_type_e::vaapi)) return nullptr;
    const bool want_hdr = config.aurora_gpu_hevc && config.dynamicRange == 1;
    const bool gpu_frames = !config.aurora_gpu_hevc;
    const bool readback = config.aurora_gpu_hevc;
    auto display = std::make_shared<portal::display_t>();
    if (display->init(type, want_hdr, gpu_frames, readback, config.width, config.height)) return display;
    if ((gpu_frames && type != mem_type_e::system) || (readback && !want_hdr)) {
      BOOST_LOG(warning) << "Portal GPU-frame capture failed; retrying with CPU frames";
      display.reset();
      display = std::make_shared<portal::display_t>();
      if (display->init(type, false, false, false)) return display;
    }
    if (!want_hdr) return nullptr;
    BOOST_LOG(warning) << "Portal HDR initialization failed; retrying SDR capture";
    display.reset();
    display = std::make_shared<portal::display_t>();
    if (display->init(type, false, false, true)) return display;
    display.reset();
    display = std::make_shared<portal::display_t>();
    return display->init(type, false, false, false) ? display : nullptr;
  }
} // namespace platf
