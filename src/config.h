/**
 * @file src/config.h
 * @brief Declarations for the configuration of Sunshine.
 */
#pragma once

// standard includes
#include <bitset>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// local includes
#include "nvenc/nvenc_config.h"

namespace config {
  // track modified config options
  inline std::unordered_map<std::string, std::string> modified_config_settings;

  struct video_t {
    bool headless_mode;
    bool limit_framerate;
    bool double_refreshrate;
    // ffmpeg params
    int qp;  // higher == more compression and less quality

    int hevc_mode;
    int av1_mode;

    int min_threads;  // Minimum number of threads/slices for CPU encoding

    // Vendor-neutral PERIODIC GOP for the avcodec encoders -- AMF (AMD), QSV (Intel),
    // VAAPI and software. 0 keeps the stock infinite GOP. `nvenc_gop_length` is the
    // equivalent for the NATIVE nvenc encoder, which bypasses avcodec entirely and so
    // could never be reached by this one.
    int gop_length;

    // CO-OP SEAM GUARD, TOP EDGE. Rows of BLACK padding placed ABOVE the captured content
    // instead of splitting the aspect-ratio bars evenly.
    //
    // WHY: in co-op the two panes are spliced into ONE picture on the client, so each host's
    // pane boundary stops being a picture boundary. The encoder still predicts across it with
    // EDGE EXTENSION (a motion vector past the edge, and the 6-tap sub-pel filter that reaches
    // 3 samples further, both clamp to the last real row), while the composite puts the OTHER
    // host's pixels there. Measured on the TV client 2026-09-04: the composite reproduces the
    // peer's pane to ~0.9 luma everywhere except its seam edge, where it is 28.3 across all 60
    // sampled columns, exact at an IRAP and never recovering afterwards.
    //
    // The existing guard adds rows BELOW the content (the capture simply starts at y=0 and asks
    // for more), which protects the pane that sits ON TOP of the composite -- its downward
    // vectors land in its own extra rows. The pane on the BOTTOM has its seam at its TOP edge
    // and nothing above it. This puts black rows there, which the encoder codes ITSELF, so what
    // the composite contains is what the encoder predicted from. Static black codes as zero-MV
    // SKIP, so it costs almost no bitrate.
    //
    // 0 = stock behaviour, bars split evenly, byte-identical to every build before this.
    int coop_top_pad;

    // CO-OP SEAM PADDING, expressed where it actually works: shrink the VIRTUAL DISPLAY by
    // 2 * coop_pad_rows and leave the ENCODE at the client's requested size, so Apollo's own
    // centring produces `coop_pad_rows` BLACK ROWS at the top and bottom of every encoded frame.
    //
    // WHY BLACK ROWS AT THE PANE EDGE FIX THE CO-OP SEAM (measured 2026-09-05 on the rig).
    // H.264 edge extension assumes the row above a picture's first row is a REPEAT of that first
    // row. In a spliced composite that row is really the OTHER host's pixels, so the assumption
    // is violated and the mismatch is injected into the reference chain every frame. If both
    // panes carry black padding at the seam the assumption comes true: the encoder predicts from
    // black and the composite really does have black there. Measured with a 16-row pad per edge,
    // peer rows 0-8 and 8-16 reproduce inside the composite at 0.088 and 0.011 luma levels --
    // essentially bit-exact -- and the peak seam ratio in the first content rows falls from
    // 1.025 to 0.632.
    //
    // WHY NOT JUST ASK FOR A TALLER STREAM, which is what the client's guard band does. That
    // makes the COMPOSITE taller than the panel (2 * 1120 = 2240 against 2160), and the display
    // pillarboxes it: measured 64 px and 74 px of black down the sides, 3702 px of 3840 active.
    // Padding INSIDE the requested height keeps the composite at 2176 and the bars at 0-8 px.
    // The cost is 2 * coop_pad_rows rows of desktop height per host, which is the cheaper trade.
    //
    // Keep it a multiple of 16 so the content stays macroblock-aligned and no resampling happens:
    // the capture then fits the encode frame exactly in width and is merely offset vertically.
    //
    // 0 = stock behaviour, no inset, byte-identical to every build before this.
    int coop_pad_rows;

    struct {
      std::string sw_preset;
      std::string sw_tune;
      std::optional<int> svtav1_preset;
    } sw;

    nvenc::nvenc_config nv;
    bool nv_realtime_hags;
    bool nv_opengl_vulkan_on_dxgi;
    bool nv_sunshine_high_power_mode;

    struct {
      int preset;
      int multipass;
      int h264_coder;
      int aq;
      int vbv_percentage_increase;
    } nv_legacy;

    struct {
      std::optional<int> qsv_preset;
      std::optional<int> qsv_cavlc;
      bool qsv_slow_hevc;
    } qsv;

    struct {
      std::optional<int> amd_usage_h264;
      std::optional<int> amd_usage_hevc;
      std::optional<int> amd_usage_av1;
      std::optional<int> amd_rc_h264;
      std::optional<int> amd_rc_hevc;
      std::optional<int> amd_rc_av1;
      std::optional<int> amd_enforce_hrd;
      std::optional<int> amd_quality_h264;
      std::optional<int> amd_quality_hevc;
      std::optional<int> amd_quality_av1;
      std::optional<int> amd_preanalysis;
      std::optional<int> amd_vbaq;
      // ROLLING INTRA REFRESH for the AMF encoder. Unset = never passed to ffmpeg, so an
      // absent config key leaves encoding byte-identical to stock.
      std::optional<int> amd_intra_refresh_mb;
      int amd_coder;
    } amd;

    struct {
      int vt_allow_sw;
      int vt_require_sw;
      int vt_realtime;
      int vt_coder;
    } vt;

    struct {
      bool strict_rc_buffer;
    } vaapi;

    std::string capture;
    std::string encoder;
    std::string adapter_name;
    std::string output_name;

    struct dd_t {
      struct workarounds_t {
        std::chrono::milliseconds hdr_toggle_delay;  ///< Specify whether to apply HDR high-contrast color workaround and what delay to use.
      };

      enum class config_option_e {
        disabled,  ///< Disable the configuration for the device.
        verify_only,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_active,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_primary,  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
        ensure_only_display  ///< @seealso{display_device::SingleDisplayConfiguration::DevicePreparation}
      };

      enum class resolution_option_e {
        disabled,  ///< Do not change resolution.
        automatic,  ///< Change resolution and use the one received from Moonlight.
        manual  ///< Change resolution and use the manually provided one.
      };

      enum class refresh_rate_option_e {
        disabled,  ///< Do not change refresh rate.
        automatic,  ///< Change refresh rate and use the one received from Moonlight.
        manual  ///< Change refresh rate and use the manually provided one.
      };

      enum class hdr_option_e {
        disabled,  ///< Do not change HDR settings.
        automatic  ///< Change HDR settings and use the state requested by Moonlight.
      };

      struct mode_remapping_entry_t {
        std::string requested_resolution;
        std::string requested_fps;
        std::string final_resolution;
        std::string final_refresh_rate;
      };

      struct mode_remapping_t {
        std::vector<mode_remapping_entry_t> mixed;  ///< To be used when `resolution_option` and `refresh_rate_option` is set to `automatic`.
        std::vector<mode_remapping_entry_t> resolution_only;  ///< To be use when only `resolution_option` is set to `automatic`.
        std::vector<mode_remapping_entry_t> refresh_rate_only;  ///< To be use when only `refresh_rate_option` is set to `automatic`.
      };

      config_option_e configuration_option;
      resolution_option_e resolution_option;
      std::string manual_resolution;  ///< Manual resolution in case `resolution_option == resolution_option_e::manual`.
      refresh_rate_option_e refresh_rate_option;
      std::string manual_refresh_rate;  ///< Manual refresh rate in case `refresh_rate_option == refresh_rate_option_e::manual`.
      hdr_option_e hdr_option;
      std::chrono::milliseconds config_revert_delay;  ///< Time to wait until settings are reverted (after stream ends/app exists).
      bool config_revert_on_disconnect;  ///< Specify whether to revert display configuration on client disconnect.
      mode_remapping_t mode_remapping;
      workarounds_t wa;
    } dd;

    int max_bitrate;  // Maximum bitrate, sets ceiling in kbps for bitrate requested from client
    double minimum_fps_target;  ///< Lowest framerate that will be used when streaming. Range 0-1000, 0 = half of client's requested framerate.

    std::string fallback_mode;
    bool isolated_virtual_display_option;
    bool ignore_encoder_probe_failure;
  };

  struct audio_t {
    std::string sink;
    std::string virtual_sink;
    bool stream;
    bool install_steam_drivers;
    bool keep_default;
    bool auto_capture;
  };

  constexpr int ENCRYPTION_MODE_NEVER = 0;  // Never use video encryption, even if the client supports it
  constexpr int ENCRYPTION_MODE_OPPORTUNISTIC = 1;  // Use video encryption if available, but stream without it if not supported
  constexpr int ENCRYPTION_MODE_MANDATORY = 2;  // Always use video encryption and refuse clients that can't encrypt

  struct stream_t {
    std::chrono::milliseconds ping_timeout;

    std::string file_apps;

    int fec_percentage;

    // Video encryption settings for LAN and WAN streams
    int lan_encryption_mode;
    int wan_encryption_mode;
  };

  struct nvhttp_t {
    // Could be any of the following values:
    // pc|lan|wan
    std::string origin_web_ui_allowed;

    std::string pkey;
    std::string cert;

    std::string sunshine_name;

    std::string file_state;

    std::string external_ip;
  };

  struct input_t {
    std::unordered_map<int, int> keybindings;

    std::chrono::milliseconds back_button_timeout;
    std::chrono::milliseconds key_repeat_delay;
    std::chrono::duration<double> key_repeat_period;

    std::string gamepad;
    bool ds4_back_as_touchpad_click;
    bool motion_as_ds4;
    bool touchpad_as_ds4;

    bool keyboard;
    bool mouse;
    bool controller;

    bool always_send_scancodes;

    bool high_resolution_scrolling;
    bool native_pen_touch;

    bool enable_input_only_mode;
    bool forward_rumble;
  };

  namespace flag {
    enum flag_e : std::size_t {
      PIN_STDIN = 0,  ///< Read PIN from stdin instead of http
      FRESH_STATE,  ///< Do not load or save state
      FORCE_VIDEO_HEADER_REPLACE,  ///< force replacing headers inside video data
      UPNP,  ///< Try Universal Plug 'n Play
      CONST_PIN,  ///< Use "universal" pin
      FLAG_SIZE  ///< Number of flags
    };
  }  // namespace flag

  struct prep_cmd_t {
    prep_cmd_t(std::string &&do_cmd, std::string &&undo_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)),
        undo_cmd(std::move(undo_cmd)),
        elevated(std::move(elevated)) {
    }

    explicit prep_cmd_t(std::string &&do_cmd, bool &&elevated):
        do_cmd(std::move(do_cmd)),
        elevated(std::move(elevated)) {
    }

    std::string do_cmd;
    std::string undo_cmd;
    bool elevated;
  };

  struct server_cmd_t {
    server_cmd_t(std::string &&cmd_name, std::string &&cmd_val, bool &&elevated):
        cmd_name(std::move(cmd_name)),
        cmd_val(std::move(cmd_val)),
        elevated(std::move(elevated)) {
    }
    std::string cmd_name;
    std::string cmd_val;
    bool elevated;
  };

  struct sunshine_t {
    bool hide_tray_controls;
    bool enable_pairing;
    bool enable_discovery;
    bool envvar_compatibility_mode;
    std::string locale;
    int min_log_level;
    std::bitset<flag::FLAG_SIZE> flags;
    std::string credentials_file;

    std::string username;
    std::string password;
    std::string salt;

    std::string config_file;

    struct cmd_t {
      std::string name;
      int argc;
      char **argv;
    } cmd;

    std::uint16_t port;
    std::string address_family;

    std::string log_file;
    bool notify_pre_releases;
    bool legacy_ordering;
    std::vector<prep_cmd_t> prep_cmds;
    std::vector<prep_cmd_t> state_cmds;
    std::vector<server_cmd_t> server_cmds;
  };

  extern video_t video;
  extern audio_t audio;
  extern stream_t stream;
  extern nvhttp_t nvhttp;
  extern input_t input;
  extern sunshine_t sunshine;

  int parse(int argc, char *argv[]);
  std::unordered_map<std::string, std::string> parse_config(const std::string_view &file_content);
}  // namespace config
