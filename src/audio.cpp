/**
 * @file src/audio.cpp
 * @brief Definitions for audio capture and encoding.
 */
// standard includes
#include <atomic>
#include <fstream>
#include <thread>

// lib includes
#include <opus/opus_multistream.h>

// local includes
#include "audio.h"
#include "config.h"
#include "globals.h"
#include "logging.h"
#include "platform/common.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  using namespace std::literals;
  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<float>>>;

  static int start_audio_control(audio_ctx_t &ctx);
  static void stop_audio_control(audio_ctx_t &);
  static void restore_sink_later(std::string id, std::string name);
  static void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params);

  int map_stream(int channels, bool quality);

  constexpr auto SAMPLE_RATE = 48000;

  // NOTE: If you adjust the bitrates listed here, make sure to update the
  // corresponding bitrate adjustment logic in rtsp_stream::cmd_announce()
  opus_stream_config_t stream_configs[MAX_STREAM_CONFIG] {
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      96000,
    },
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      512000,
    },
    {
      SAMPLE_RATE,
      6,
      4,
      2,
      platf::speaker::map_surround51,
      256000,
    },
    {
      SAMPLE_RATE,
      6,
      6,
      0,
      platf::speaker::map_surround51,
      1536000,
    },
    {
      SAMPLE_RATE,
      8,
      5,
      3,
      platf::speaker::map_surround71,
      450000,
    },
    {
      SAMPLE_RATE,
      8,
      8,
      0,
      platf::speaker::map_surround71,
      2048000,
    },
  };

  void encodeThread(sample_queue_t samples, config_t config, void *channel_data) {
    auto packets = mail::man->queue<packet_t>(mail::audio_packets);
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    // Encoding takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    opus_t opus {opus_multistream_encoder_create(
      stream.sampleRate,
      stream.channelCount,
      stream.streams,
      stream.coupledStreams,
      stream.mapping,
      OPUS_APPLICATION_RESTRICTED_LOWDELAY,
      nullptr
    )};

    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_BITRATE(stream.bitrate));
    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_VBR(0));

    BOOST_LOG(info) << "Opus initialized: "sv << stream.sampleRate / 1000 << " kHz, "sv
                    << stream.channelCount << " channels, "sv
                    << stream.bitrate / 1000 << " kbps (total), LOWDELAY"sv;

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    while (auto sample = samples->pop()) {
      buffer_t packet {1400};

      int bytes = opus_multistream_encode_float(opus.get(), sample->data(), frame_size, std::begin(packet), packet.size());
      if (bytes < 0) {
        BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);
        packets->stop();

        return;
      }

      packet.fake_resize(bytes);
      packets->raise(channel_data, std::move(packet));
    }
  }

  void capture(safe::mail_t mail, config_t config, void *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    if (!config::audio.stream || config.input_only) {
      shutdown_event->view();
      return;
    }
    auto stream = stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];
    if (config.flags[config_t::CUSTOM_SURROUND_PARAMS]) {
      apply_surround_params(stream, config.customStreamParams);
    }

    auto ref = get_audio_ctx_ref();
    if (!ref) {
      return;
    }

    auto init_failure_fg = util::fail_guard([&shutdown_event]() {
      BOOST_LOG(error) << "Unable to initialize audio capture. The stream will not have audio."sv;

      // Wait for shutdown to be signalled if we fail init.
      // This allows streaming to continue without audio.
      shutdown_event->view();
      return;
    });

    auto &control = ref->control;
    if (!control) {
      return;
    }

    // Order of priority:
    // 1. Virtual sink
    // 2. Audio sink
    // 3. Host
    std::string *sink = &ref->sink.host;
    if (!config::audio.sink.empty()) {
      sink = &config::audio.sink;
    }

    // Prefer the virtual sink if host playback is disabled or there's no other sink
    if (ref->sink.null && (!config.flags[config_t::HOST_AUDIO] || sink->empty())) {
      auto &null = *ref->sink.null;
      switch (stream.channelCount) {
        case 2:
          sink = &null.stereo;
          break;
        case 6:
          sink = &null.surround51;
          break;
        case 8:
          sink = &null.surround71;
          break;
      }
    }

    BOOST_LOG(info) << "Selected audio sink: "sv << *sink;

    // Only the first to start a session may change the default sink
    if (!ref->sink_flag->exchange(true, std::memory_order_acquire)) {
      // If the selected sink is different than the current one, change sinks.
      ref->restore_sink = ref->sink.host != *sink;
      if (ref->restore_sink) {
        if (control->set_sink(*sink)) {
          return;
        }
      }
    }

    auto frame_size = config.packetDuration * stream.sampleRate / 1000;
    auto mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size);
    if (!mic) {
      return;
    }

    // Audio is initialized, so we don't want to print the failure message
    init_failure_fg.disable();

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    auto samples = std::make_shared<sample_queue_t::element_type>(30);
    std::thread thread {encodeThread, samples, config, channel_data};

    auto fg = util::fail_guard([&]() {
      samples->stop();
      thread.join();

      shutdown_event->view();
    });

    int samples_per_frame = frame_size * stream.channelCount;

    while (!shutdown_event->peek()) {
      std::vector<float> sample_buffer;
      sample_buffer.resize(samples_per_frame);

      auto status = mic->sample(sample_buffer);
      switch (status) {
        case platf::capture_e::ok:
          break;
        case platf::capture_e::timeout:
          continue;
        case platf::capture_e::reinit:
          if (config::audio.auto_capture) {
            BOOST_LOG(info) << "Reinitializing audio capture"sv;
            mic.reset();
            do {
              mic = control->microphone(stream.mapping, stream.channelCount, stream.sampleRate, frame_size);
              if (!mic) {
                BOOST_LOG(warning) << "Couldn't re-initialize audio input"sv;
              }
            } while (!mic && !shutdown_event->view(5s));
          }

          continue;
        default:
          return;
      }

      samples->raise(std::move(sample_buffer));
    }
  }

  audio_ctx_ref_t get_audio_ctx_ref() {
    static auto control_shared {safe::make_shared<audio_ctx_t>(start_audio_control, stop_audio_control)};
    return control_shared.ref();
  }

  bool is_audio_ctx_sink_available(const audio_ctx_t &ctx) {
    if (!ctx.control) {
      return false;
    }

    const std::string &sink = ctx.sink.host.empty() ? config::audio.sink : ctx.sink.host;
    if (sink.empty()) {
      return false;
    }

    // Umbra: or the same device, back under a new id.
    return ctx.control->is_sink_available(sink) ||
           (!ctx.sink.host_name.empty() && ctx.control->is_sink_available(ctx.sink.host_name));
  }

  int map_stream(int channels, bool quality) {
    int shift = quality ? 1 : 0;
    switch (channels) {
      case 2:
        return STEREO + shift;
      case 6:
        return SURROUND51 + shift;
      case 8:
        return SURROUND71 + shift;
    }
    return STEREO;
  }

  /*
   * Umbra: putting the host's own default playback device back after streaming.
   *
   * Stock behaviour snapshots the default device's ID when audio starts and sets it back when the
   * audio context ends, "best effort". On a real PC that failed routinely: the default is often a
   * monitor's HDMI/DP audio, which is ABSENT while the panel is off for the stream and is re-created
   * under a NEW id when displays change, so the restore logged "Couldn't find audio sink {id}" and the
   * PC stayed on the virtual sink (Steam Streaming Speakers) -- silent. The next session then
   * snapshotted the virtual sink as "the host's device", so it never recovered by itself.
   * Hence: remember the device's name too, retry for a while in the background until it reappears,
   * and persist the last real default so a PC found on the virtual sink is set back as well.
   */
  static std::atomic<unsigned> sink_generation {0};  // bumped when a new audio context (session) starts

  static std::string saved_host_sink_path() {
    return platf::appdata().string() + "/audio_restore_sink.txt";
  }

  static void save_host_sink(const platf::sink_t &sink) {
    std::ofstream out(saved_host_sink_path(), std::ios::trunc);
    out << sink.host << '\n' << sink.host_name << '\n';
  }

  static bool load_host_sink(std::string &host, std::string &host_name) {
    std::ifstream in(saved_host_sink_path());
    if (!in || !std::getline(in, host) || host.empty()) {
      return false;
    }
    std::getline(in, host_name);
    return true;
  }

  // True if `id` is one of the virtual sinks (their names end with the device id: "virtual-Stereo{id}").
  static bool is_virtual_sink(const platf::sink_t &sink, const std::string &id) {
    if (!sink.null || id.empty()) {
      return false;
    }
    for (const std::string *v : {&sink.null->stereo, &sink.null->surround51, &sink.null->surround71}) {
      if (v->size() >= id.size() && v->compare(v->size() - id.size(), id.size(), id) == 0) {
        return true;
      }
    }
    return false;
  }

  int start_audio_control(audio_ctx_t &ctx) {
    auto fg = util::fail_guard([]() {
      BOOST_LOG(warning) << "There will be no audio"sv;
    });

    ++sink_generation;  // abandons any restore still waiting from the previous session

    ctx.sink_flag = std::make_unique<std::atomic_bool>(false);

    // The default sink has not been replaced yet.
    ctx.restore_sink = false;

    if (!(ctx.control = platf::audio_control())) {
      return 0;
    }

    auto sink = ctx.control->sink_info();
    if (!sink) {
      // Let the calling code know it failed
      ctx.control.reset();
      return 0;
    }

    ctx.sink = std::move(*sink);

    // Umbra: a PC found on the virtual sink was never set back (failed restore, crash): restore the last
    // real default when this session ends. Otherwise remember this one for next time.
    if (is_virtual_sink(ctx.sink, ctx.sink.host)) {
      std::string host, host_name;
      if (load_host_sink(host, host_name)) {
        BOOST_LOG(warning) << "Default audio device is the virtual sink, left over from an earlier session; "sv
                           << "will restore ["sv << (host_name.empty() ? host : host_name) << "] after streaming"sv;
        ctx.sink.host = std::move(host);
        ctx.sink.host_name = std::move(host_name);
      }
    } else if (!ctx.sink.host.empty()) {
      save_host_sink(ctx.sink);
    }

    fg.disable();
    return 0;
  }

  void stop_audio_control(audio_ctx_t &ctx) {
    // restore audio-sink if applicable
    if (!ctx.restore_sink) {
      return;
    }

    // Change back to the host sink, unless there was none
    const std::string &sink = ctx.sink.host.empty() ? config::audio.sink : ctx.sink.host;
    if (sink.empty()) {
      return;
    }
    if (ctx.control->is_sink_available(sink) && ctx.control->set_sink(sink) == 0) {
      return;
    }
    // Umbra: the same device under a new id (display change) is found by its name.
    const std::string &name = ctx.sink.host_name;
    if (!name.empty() && ctx.control->is_sink_available(name) && ctx.control->set_sink(name) == 0) {
      BOOST_LOG(info) << "Restored default audio device by name ["sv << name << ']';
      return;
    }
    // Absent for now -- e.g. its monitor is only switched back on after the session: keep trying in the
    // background rather than leave the PC on the virtual sink.
    BOOST_LOG(info) << "Default audio device ["sv << (name.empty() ? sink : name) << "] is not present yet; will restore it when it appears"sv;
    restore_sink_later(sink, name);
  }

  void restore_sink_later(std::string id, std::string name) {
    const unsigned generation = sink_generation.load();
    std::thread([id = std::move(id), name = std::move(name), generation]() {
      auto control = platf::audio_control();
      if (!control) {
        return;
      }
      auto shutdown_event = mail::man->event<bool>(mail::shutdown);
      const std::string &label = name.empty() ? id : name;
      for (int second = 1; second <= 60; ++second) {
        std::this_thread::sleep_for(1s);
        if (shutdown_event->peek() || sink_generation.load() != generation) {
          return;  // shutting down, or a new session owns the audio device now
        }
        if ((control->is_sink_available(id) && control->set_sink(id) == 0) ||
            (!name.empty() && control->is_sink_available(name) && control->set_sink(name) == 0)) {
          BOOST_LOG(info) << "Restored default audio device ["sv << label << "] after "sv << second << " s"sv;
          return;
        }
      }
      BOOST_LOG(warning) << "Gave up restoring the default audio device ["sv << label << "] after 60 s"sv;
    }).detach();
  }

  void apply_surround_params(opus_stream_config_t &stream, const stream_params_t &params) {
    stream.channelCount = params.channelCount;
    stream.streams = params.streams;
    stream.coupledStreams = params.coupledStreams;
    stream.mapping = params.mapping;
  }
}  // namespace audio
