// Manual live test of the actual host capture class. No encoder or TV verdict.
#include "src/platform/linux/portalgrab.cpp"
#include <csignal>
#include <iostream>
boost::log::sources::severity_logger<int> verbose, debug, info, warning, error, fatal;
namespace config { sunshine_t sunshine {}; }
namespace mail { safe::mail_t man; }
namespace platf { std::filesystem::path appdata() { return std::filesystem::current_path(); } }
static volatile std::sig_atomic_t stop = 0;
static void stopped(int) { stop = 1; }
int main(int argc, char **argv) {
  std::signal(SIGINT, stopped); std::signal(SIGTERM, stopped);
  int seconds = argc > 1 ? std::stoi(argv[1]) : 15;
  if(seconds < 1 || seconds > 120) return 2;
  auto cleanup = util::fail_guard([] { platf::portal_shutdown(); });
  if (argc > 2 && !platf::portal_prepare_virtual(1920, 2160, 165000)) return 3;
  const bool lifecycle = argc > 3;
  for (int phase = 0; phase < (lifecycle ? 2 : 1); ++phase) {
  if (phase && !platf::portal_prepare_virtual(3840, 1080, 120000)) return 4;
  auto display = std::make_shared<portal::display_t>();
  if (!display->init(platf::mem_type_e::system)) return 1;
  platf::portal_inhibit(true);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  bool cursor = true; unsigned frames = 0;
  auto status = display->capture([&](std::shared_ptr<platf::img_t> &&img, bool fresh) {
    if (fresh) {
      ++frames;
      if (frames == 1 || frames % 120 == 0) {
        std::ofstream out(phase ? "host-wide.ppm" : "host-frame.ppm", std::ios::binary);
        out << "P6\n" << img->width << ' ' << img->height << "\n255\n";
        for (int y = 0; y < img->height; ++y) for (int x = 0; x < img->width; ++x) {
          auto p = img->data + y * img->row_pitch + x * 4;
          char rgb[] = {char(p[2]), char(p[1]), char(p[0])}; out.write(rgb, 3);
        }
        std::cout << "frames=" << frames << " size=" << img->width << 'x' << img->height
                  << " timestamp=" << img->frame_timestamp.has_value() << std::endl;
      }
    }
    return !stop && std::chrono::steady_clock::now() < deadline;
  }, [&](std::shared_ptr<platf::img_t> &img) { img = display->alloc_img(); return !stop; }, &cursor);
  std::cout << "status=" << int(status) << " frames=" << frames << std::endl;
  if (status != platf::capture_e::ok || !frames) return 1;
  display.reset();
  }
  platf::portal_release_virtual();
  std::cout << "released=" << !platf::portal_virtual_active() << std::endl;
  return 0;
}
