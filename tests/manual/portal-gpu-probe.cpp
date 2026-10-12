// SPDX-License-Identifier: GPL-3.0-only
// Actual live portal capture -> production GPU adapter -> FIFO/live decoder.
// Component diagnostic only: does not qualify GameStream, co-op or HDR.
#include "src/platform/linux/portalgrab.cpp"
#include <csignal>
#include <iostream>
#include <sys/stat.h>
boost::log::sources::severity_logger<int> verbose, debug, info, warning, error, fatal;
namespace config { sunshine_t sunshine {}; video_t video {}; }
namespace mail { safe::mail_t man; }
namespace platf { std::filesystem::path appdata() { return std::filesystem::current_path(); } }
static volatile std::sig_atomic_t stopped = 0;
static void stop(int) { stopped = 1; }
int main(int argc, char **argv) {
  if (argc != 4) { std::cerr << "Usage: portal-gpu-probe seconds depth live-fifo\n"; return 2; }
  const int seconds = std::stoi(argv[1]), depth = std::stoi(argv[2]);
  if (seconds < 1 || seconds > 300 || (depth != 8 && depth != 10)) return 2;
  struct stat output {};
  if (stat(argv[3], &output) || !S_ISFIFO(output.st_mode)) {
    std::cerr << "Output must be an existing FIFO connected to a live decoder\n";
    return 2;
  }
  std::signal(SIGINT, stop); std::signal(SIGTERM, stop); std::signal(SIGPIPE, stop);
  auto cleanup = util::fail_guard([] { platf::portal_shutdown(); });
  auto display = std::make_shared<portal::display_t>();
  if (!display->init(platf::mem_type_e::system, depth == 10)) return 1;
  platf::portal_inhibit(true);
  video::config_t config {};
  config.width = (display->width + 31) & ~31;
  config.height = (display->height + 31) & ~31;
  config.videoFormat = 1; config.encoderCscMode = 2; config.dynamicRange = depth == 10;
  config.bitrate = 20000; config.framerate = 60;
  auto encoder = display->make_gpu_hevc_encode_device(config);
  if (!encoder) return 3;
  std::ofstream stream(argv[3], std::ios::binary);
  if (!stream) return 4;
  std::cout << "LIVE_READY source=" << display->width << 'x' << display->height << " coded="
            << config.width << 'x' << config.height << " depth=" << depth << " HDR=" << display->is_hdr() << "\n" << std::flush;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  bool cursor = true, failed = false;
  unsigned frames = 0;
  auto status = display->capture([&](std::shared_ptr<platf::img_t> &&img, bool fresh) {
    if (fresh) {
      std::vector<uint8_t> bytes; bool idr = false;
      const bool recovery = frames == 120;
      if (encoder->convert(*img) || !encoder->encode(frames, recovery, bytes, idr) ||
          ((frames == 0 || recovery) && !idr)) { failed = true; return false; }
      stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); stream.flush();
      if (!stream) { failed = true; return false; }
      if (frames == 0 || recovery || frames % 120 == 0)
        std::cout << "LIVE_FRAME " << frames << " IDR=" << idr << " bytes=" << bytes.size() << '\n' << std::flush;
      ++frames;
    }
    return !stopped && std::chrono::steady_clock::now() < deadline;
  }, [&](std::shared_ptr<platf::img_t> &img) { img = display->alloc_img(); return !stopped; }, &cursor);
  std::cout << "LIVE_DONE frames=" << frames << " failed=" << failed << " status=" << int(status) << '\n';
  return failed || status != platf::capture_e::ok || frames < 121;
}
