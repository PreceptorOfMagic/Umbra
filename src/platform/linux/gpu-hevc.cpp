// SPDX-License-Identifier: GPL-3.0-only
#include "gpu-hevc.h"
#include "aurora_gpu_hevc_linux.h"
#include "src/video.h"
#include <dlfcn.h>
#include <cstring>
#include <filesystem>
#include <vector>

namespace gpu_hevc {
namespace {
  const std::filesystem::path directory {SUNSHINE_GPU_HEVC_DIR};
  struct library_t {
    void *module = nullptr;
    decltype(&aghl_open) open = nullptr;
    decltype(&aghl_encode) encode = nullptr;
    decltype(&aghl_close) close = nullptr;
    decltype(&aghl_probe) probe = nullptr;
    ~library_t() { if (module) dlclose(module); }
    bool load(unsigned depth) {
      module = dlopen((directory / ("libaurora_gpu_hevc" + std::to_string(depth) + ".so")).c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!module) return false;
      open = reinterpret_cast<decltype(open)>(dlsym(module, "aghl_open"));
      encode = reinterpret_cast<decltype(encode)>(dlsym(module, "aghl_encode"));
      close = reinterpret_cast<decltype(close)>(dlsym(module, "aghl_close"));
      probe = reinterpret_cast<decltype(probe)>(dlsym(module, "aghl_probe"));
      return open && encode && close && probe;
    }
  };
  class device_t final: public platf::gpu_hevc_encode_device_t {
  public:
    ~device_t() override { if (session) library.close(session); }
    bool init(const video::config_t &config, bool hdr) {
      if (hdr) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] HDR capture conversion is unavailable for the Linux CPU adapter";
        return false;
      }
      colorspace = video::colorspace_from_client_config(config, hdr);
      if (config.videoFormat != 1 || config.chromaSamplingType != 0 || colorspace.full_range ||
          (colorspace.colorspace != video::colorspace_e::rec709 && colorspace.colorspace != video::colorspace_e::bt2020)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] unsupported Linux geometry/colour contract; no fallback";
        return false;
      }
      if (!library.load(colorspace.bit_depth)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] Linux library unavailable in " << directory;
        return false;
      }
      kernels = (directory / "kernels").string();
      aghl_config args {sizeof(aghl_config), AGHL_ABI_VERSION, uint32_t(config.width), uint32_t(config.height),
        colorspace.bit_depth, video::colorspace_is_hdr(colorspace) ? 1u : 0u, kernels.c_str(),
        uint32_t(config.bitrate), uint32_t(config.framerate)};
      char detail[1024] {};
      session = library.open(&args, detail, sizeof detail);
      if (!session) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] Linux open failed: " << detail;
        return false;
      }
      BOOST_LOG(info) << "[AURORA-GPU-HEVC] Linux opened " << config.width << 'x' << config.height
        << " depth=" << colorspace.bit_depth << " HDR=" << args.hdr << " bitrate_kbps=" << args.bitrate_kbps
        << " rate_control=bitrate capture_upload=cpu experimental=1";
      return true;
    }
    int convert(platf::img_t &img) override {
      ready = false;
      // This adapter is attached only to CPU BGRA capturers. GPU handles and
      // packed 10-bit/PQ buffers must never be interpreted as BGRA/scRGB.
      if (video::colorspace_is_hdr(colorspace) || !img.data || img.width < 1 || img.height < 1 ||
          img.width > 8192 || img.height > 8192 || img.pixel_pitch != 4 || img.row_pitch < img.width * 4) return -1;
      const size_t row = size_t(img.width) * 4;
      pixels.resize(row * img.height);
      for (int y = 0; y < img.height; ++y)
        std::memcpy(pixels.data() + y * row, img.data + size_t(y) * img.row_pitch, row);
      frame = {sizeof(aghl_frame), uint32_t(img.width), uint32_t(img.height), AGHL_BGRA8, row, pixels.size(), pixels.data()};
      ready = true;
      return 0;
    }
    bool encode(uint64_t index, bool force_idr, std::vector<uint8_t> &bytes, bool &idr) override {
      if (!session || !ready) return false;
      agh_packet packet {}; packet.size = sizeof packet;
      char detail[1024] {};
      if (library.encode(session, &frame, index, force_idr ? 1u : 0u, &packet, detail, sizeof detail) ||
          packet.frame_id != index || !packet.data || !packet.bytes) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] Linux encode failed: " << detail;
        return false;
      }
      bytes.assign(packet.data, packet.data + packet.bytes);
      idr = packet.is_idr != 0;
      if (idr || index % 300 == 0)
        BOOST_LOG(info) << "[AURORA-GPU-HEVC] frame=" << index << " bytes=" << packet.bytes << " IDR=" << idr
          << " QP=" << packet.qp << " passes=" << packet.encode_passes << " submit_packet_ms=" << packet.submit_to_packet_ms;
      return true;
    }
  private:
    library_t library;
    aghl_session *session = nullptr;
    std::string kernels;
    std::vector<uint8_t> pixels;
    aghl_frame frame {};
    bool ready = false;
  };
}
  bool available() {
    // Hardware/driver changes require restarting the host. Discovery never
    // opens a screen-sharing prompt or starts a synthetic encode.
    static const bool usable = [] {
      std::error_code ec;
      for (const char *name : {"cabac.cl", "cabac_parallel.cl", "capture.cl", "coefficients.cl", "motion.cl",
                              "picture.cl", "prediction.cl", "residual.cl", "linux_capture.cl"})
        if (!std::filesystem::is_regular_file(directory / "kernels" / name, ec)) return false;
      for (unsigned depth : {8u, 10u}) {
        library_t lib;
        if (!lib.load(depth)) return false;
        char name[512] {}, error[1024] {};
        if (lib.probe(name, sizeof name, error, sizeof error)) {
          BOOST_LOG(warning) << "[AURORA-GPU-HEVC] unavailable: " << error;
          return false;
        }
      }
      return true;
    }();
    return usable;
  }
  std::unique_ptr<platf::gpu_hevc_encode_device_t> make_device(const video::config_t &config, bool hdr) {
    auto device = std::make_unique<device_t>();
    if (!device->init(config, hdr)) return {};
    return device;
  }
}
