#include "beta_profile.h"
#include "config.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace config { video_t video {11, 2, {false}}; }
namespace platf {
  std::filesystem::path directory;
  std::filesystem::path appdata() { return directory; }
}

void profile(const std::string &body) {
  std::ofstream(platf::directory / "beta_profile.conf") << "client_uuid = beta-cert\n" << body;
}
bool rejects(const std::string &uuid) {
  try {
    auto lease = beta_profile::acquire_for_client("Legacy Moonlight Client", uuid);
    return false;
  } catch (const std::runtime_error &) { return true; }
}
void baseline() {
  assert(config::video.gop_length == 11);
  assert(config::video.coop_pad_rows == 2);
  assert(!config::video.nv.h264_main_profile);
}
void mixed() {
  assert(config::video.gop_length == 300);
  assert(config::video.coop_pad_rows == 16);
  assert(config::video.nv.h264_main_profile);
}
auto acquire(const std::string &uuid = "beta-cert") {
  return beta_profile::acquire_for_client("Legacy Moonlight Client", uuid);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  platf::directory = argv[1];
  { auto absent = acquire("live-cert"); baseline(); }
  profile("gop_length = 300\ncoop_pad_rows = 16\nnvenc_h264_main = true\n");
  { auto live = acquire("live-cert"); baseline();
    auto live2 = acquire("another-live-cert"); baseline();
    assert(rejects("beta-cert")); baseline(); }
  baseline();

  // A failed HTTP launch explicitly releases its lease even when proc retains
  // the launch object. Pending RTSP expiry has the same ownership operation.
  struct launch { beta_profile::lease_t settings; };
  auto pending = std::make_shared<launch>();
  auto retained_proc = pending;
  pending->settings = acquire(); mixed();
  beta_profile::release(pending->settings); baseline();
  pending->settings = acquire(); mixed();
  beta_profile::release(pending->settings); baseline();

  // The real stream copies ownership BEFORE video threads start. Clearing the
  // RTSP launch/control handshake cannot restore settings underneath the encoder.
  pending->settings = acquire();
  auto streaming = beta_profile::retain(pending->settings);
  beta_profile::release(pending->settings); mixed();
  std::thread encoder([] { mixed(); }); encoder.join();
  assert(rejects("live-cert")); mixed();
  auto second_stream = acquire();
  streaming.reset(); mixed();
  second_stream.reset(); baseline();
  assert(retained_proc);  // app object survives after settings have been restored

  // Parse every key before applying any: malformed bool/integer, unknown key,
  // overflow and trailing junk cannot leave a partly applied profile.
  for (const auto &bad : {"nvenc_h264_main = 2", "nvenc_h264_main = truejunk",
                          "gop_length = 300junk", "coop_pad_rows = -1",
                          "gop_length = 999999999999999", "unknown = 1"}) {
    profile("gop_length = 300\n" + std::string(bad) + "\n");
    assert(rejects("beta-cert")); baseline();
    auto live = acquire("live-cert"); baseline();
  }
  for (const auto &value : {"true", "1"}) {
    profile("nvenc_h264_main = " + std::string(value) + "\n");
    { auto lease = acquire(); assert(config::video.nv.h264_main_profile); }
    baseline();
  }
  config::video.nv.h264_main_profile = true;
  for (const auto &value : {"false", "0"}) {
    profile("nvenc_h264_main = " + std::string(value) + "\n");
    { auto lease = acquire(); assert(!config::video.nv.h264_main_profile); }
    assert(config::video.nv.h264_main_profile);
  }
  config::video.nv.h264_main_profile = false;
  profile("nvenc_h264_main = true\nnvenc_h264_main = false\n");
  { auto lease = acquire(); baseline(); } baseline();

  profile("nvenc_h264_main = true\n");
  { auto lease = acquire();
    profile("nvenc_h264_main = false\n");
    assert(rejects("beta-cert")); assert(config::video.nv.h264_main_profile); }
  baseline();

  // Exercise last-owner destruction racing a new acquisition. A stale lease's
  // destructor must not restore the newer generation's active settings.
  profile("gop_length = 300\ncoop_pad_rows = 16\nnvenc_h264_main = true\n");
  for (int i = 0; i < 1000; ++i) {
    auto lease = acquire();
    std::atomic<bool> go {false};
    std::thread drop([lease = std::move(lease), &go]() mutable {
      while (!go.load()) {};
      lease.reset();
    });
    go.store(true);
    auto next = acquire();
    drop.join(); mixed();
    next.reset(); baseline();
  }
  // HTTP cancel can reset the pending slot concurrently with RTSP allocation.
  // Retaining either wins an intact lease or reports the already-cancelled launch.
  for (int i = 0; i < 1000; ++i) {
    pending->settings = acquire();
    std::atomic<bool> go {false};
    std::thread cancel([&] {
      while (!go.load()) {};
      beta_profile::release(pending->settings);
    });
    go.store(true);
    auto allocated = beta_profile::retain(pending->settings);
    cancel.join();
    if (allocated) { mixed(); } else { baseline(); }
    allocated.reset(); baseline();
  }
  std::cout << "PASS: typed profile parsing, atomic rejection, UUID isolation, HTTP failure/RTSP timeout ownership, control-to-encoder lifetime, close restoration, compatible sharing, incompatible rejection, 1000 concurrent release/reacquire cycles and 1000 cancel/allocation races\n";
}
