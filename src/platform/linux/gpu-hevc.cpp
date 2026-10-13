// SPDX-License-Identifier: GPL-3.0-only
#include "gpu-hevc.h"
#include "aurora_gpu_hevc_linux.h"
#include "gpu-hevc-frames.h"
#include "graphics.h"
#include "src/video.h"
#include "src/config.h"
#include <dlfcn.h>
#include <cstring>
#include <filesystem>
#include <vector>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <regex>

namespace gpu_hevc {
namespace {
  const std::filesystem::path directory {SUNSHINE_GPU_HEVC_DIR};
  std::string device_selector() {
    const auto &configured = config::video.adapter_name;
    if (configured.empty() || configured.front() != '/') return configured;
    struct stat node {};
    if (stat(configured.c_str(), &node) || !S_ISCHR(node.st_mode))
      throw std::runtime_error("Graphics Adapter is not an accessible DRM device: " + configured);
    std::error_code ec;
    const auto device = std::filesystem::canonical(std::filesystem::path("/sys/dev/char") /
      (std::to_string(major(node.st_rdev)) + ":" + std::to_string(minor(node.st_rdev))) / "device", ec);
    const auto pci = device.filename().string();
    if (ec || !std::regex_match(pci, std::regex("[0-9a-f]{4}:[0-9a-f]{2}:[0-1][0-9a-f]\\.[0-7]")))
      throw std::runtime_error("Graphics Adapter PCI identity is unavailable: " + configured);
    return "pci:" + pci;
  }
  struct library_t {
    void *module = nullptr;
    decltype(&aghl_open) open = nullptr;
    decltype(&aghl_encode) encode = nullptr;
    decltype(&aghl_close) close = nullptr;
    decltype(&aghl_probe_device) probe = nullptr;
    decltype(&aghl_gl_sharing) gl_sharing = nullptr;  // optional: libraries before GL-shared frames lack it
    ~library_t() { if (module) dlclose(module); }
    bool load(unsigned depth) {
      module = dlopen((directory / ("libaurora_gpu_hevc" + std::to_string(depth) + ".so")).c_str(), RTLD_NOW | RTLD_LOCAL);
      if (!module) return false;
      open = reinterpret_cast<decltype(open)>(dlsym(module, "aghl_open"));
      encode = reinterpret_cast<decltype(encode)>(dlsym(module, "aghl_encode"));
      close = reinterpret_cast<decltype(close)>(dlsym(module, "aghl_close"));
      probe = reinterpret_cast<decltype(probe)>(dlsym(module, "aghl_probe_device"));
      gl_sharing = reinterpret_cast<decltype(gl_sharing)>(dlsym(module, "aghl_gl_sharing"));
      return open && encode && close && probe;
    }
  };
  class device_t final: public platf::gpu_hevc_encode_device_t {
  public:
    ~device_t() override { if (session) library.close(session); }
    bool init(const video::config_t &config, bool hdr, capture_format_e format, bool frames_from_gpu) {
      capture_format = format;
      gpu_frames = frames_from_gpu;
      if (hdr != (format == capture_format_e::bt2020_pq_xbgr2101010)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] capture color format does not match HDR state";
        return false;
      }
      colorspace = video::colorspace_from_client_config(config, hdr);
      // The custom SDR conversion and VUI are Rec.709. A client may request
      // Rec.2020 for its HDR-capable display even when capture falls back to SDR.
      if (!hdr && colorspace.colorspace == video::colorspace_e::bt2020sdr) {
        BOOST_LOG(info) << "[AURORA-GPU-HEVC] SDR capture uses Rec.709 instead of requested Rec.2020 SDR";
        colorspace.colorspace = video::colorspace_e::rec709;
      }
      if (config.videoFormat != 1 || config.chromaSamplingType != 0 || colorspace.full_range ||
          (colorspace.colorspace != video::colorspace_e::rec709 && colorspace.colorspace != video::colorspace_e::bt2020)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] unsupported Linux colour contract: codec=" << config.videoFormat
          << " chroma=" << config.chromaSamplingType << " full_range=" << colorspace.full_range
          << " csc=" << config.encoderCscMode << "; no fallback";
        return false;
      }
      if (!library.load(colorspace.bit_depth)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] Linux library unavailable in " << directory;
        return false;
      }
      kernels = (directory / "kernels").string();
      try { selector = device_selector(); }
      catch (const std::exception &failure) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] " << failure.what(); return false;
      }
      char selected[512] {}, detail[1024] {};
      if (library.probe(selector.c_str(), selected, sizeof selected, detail, sizeof detail)) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] GPU selection failed: " << detail; return false;
      }
      // "<name> pci:dddd:bb:dd.f" where OpenCL reports it (cl_khr_pci_bus_info): the GPU-frame
      // hand-off must reach the same GPU. Without it GPU frames are read back.
      if (const char *at = std::strstr(selected, " pci:")) pci = at + 5;
      // GPU frames: the stage exists before the encoder session so that, where CUDA does not hand
      // frames over, the session's OpenCL context can share the stage's EGL context. A stage that
      // cannot be created leaves `frames` empty: convert() fails and the capture reinitialises to
      // read-back (gpu_frames_possible() is false by then).
      void *egl_display = nullptr, *egl_context = nullptr;
      if (gpu_frames && gpu_frames_possible()) {
        frames = gpu_frames_t::create(config.width, config.height, capture_format == capture_format_e::bt2020_pq_xbgr2101010, pci);
        if (frames && std::strcmp(frames->hand_off(), "cuda") && library.gl_sharing) {
          egl_display = frames->egl_display();
          egl_context = frames->egl_context();
        }
      }
      aghl_config args {sizeof(aghl_config), AGHL_ABI_VERSION, uint32_t(config.width), uint32_t(config.height),
        colorspace.bit_depth, video::colorspace_is_hdr(colorspace) ? 1u : 0u, kernels.c_str(),
        uint32_t(config.bitrate), uint32_t(config.framerate), selector.c_str(), egl_display, egl_context};
      session = library.open(&args, detail, sizeof detail);
      if (!session) {
        BOOST_LOG(error) << "[AURORA-GPU-HEVC] Linux open failed: " << detail;
        return false;
      }
      if (egl_context) {
        if (library.gl_sharing(session)) frames->use_gl_sharing();
        else BOOST_LOG(info) << "[AURORA-GPU-HEVC] the encoder's OpenCL does not share the stage's GL context; GPU frames are read back";
      }
      BOOST_LOG(info) << "[AURORA-GPU-HEVC] Linux opened " << config.width << 'x' << config.height
        << " depth=" << colorspace.bit_depth << " HDR=" << args.hdr << " bitrate_kbps=" << args.bitrate_kbps
        << " GPU=" << selected << " rate_control=bitrate capture_upload=" << (gpu_frames ? "gpu-frames" : "cpu") << " experimental=1";
      return true;
    }
    int convert(platf::img_t &img) override {
      ready = false;
      if (gpu_frames) return convert_gpu(img);
      // Only an explicitly typed portal image can carry PQ. Other CPU
      // capturers retain their existing BGRA contract.
      auto typed = dynamic_cast<cpu_image_t *>(&img);
      auto format = typed ? typed->capture_format : capture_format_e::bgra8;
      if (format != capture_format || !img.data || img.width < 1 || img.height < 1 ||
          img.width > 8192 || img.height > 8192 || img.pixel_pitch != 4 || img.row_pitch < img.width * 4) return -1;
      const size_t row = size_t(img.width) * 4;
      pixels.resize(row * img.height);
      for (int y = 0; y < img.height; ++y)
        std::memcpy(pixels.data() + y * row, img.data + size_t(y) * img.row_pitch, row);
      frame = {sizeof(aghl_frame), uint32_t(img.width), uint32_t(img.height), (capture_format == capture_format_e::bt2020_pq_xbgr2101010 ? AGHL_XBGR2101010_PQ : AGHL_BGRA8), row, pixels.size(), pixels.data(), 0, -1, 0, 0};
      ready = true;
      return 0;
    }
    // A captured DMA-BUF (egl::img_descriptor_t; sequence 0 = the dummy before the first capture).
    int convert_gpu(platf::img_t &img) {
      auto descriptor = dynamic_cast<egl::img_descriptor_t *>(&img);
      if (!descriptor || img.width < 1 || img.height < 1) return -1;
      const bool pq = capture_format == capture_format_e::bt2020_pq_xbgr2101010;
      // No stage (it failed when the device opened): the capture reinitialises to read-back.
      if (!frames) return -1;
      if ((frames->frame_width() != img.width || frames->frame_height() != img.height) && !frames->resize(img.width, img.height)) return -1;
      if (descriptor->sequence != 0 && (descriptor->sd.width != img.width || descriptor->sd.height != img.height)) return -1;
      if (!frames->stage(descriptor->sequence == 0 ? nullptr : &descriptor->sd)) return -1;
      const size_t row = size_t(img.width) * 4;
      frame = {sizeof(aghl_frame), uint32_t(img.width), uint32_t(img.height), pq ? AGHL_XBGR2101010_PQ : AGHL_BGRA8, row,
        frames->bytes(), nullptr, 0, -1, 0, frames->serial()};
      if (frames->external_fd() >= 0) {
        frame.flags = AGHL_FRAME_EXTERNAL;
        frame.external_fd = frames->external_fd();
      } else if (frames->gl_texture()) {
        frame.flags = AGHL_FRAME_GL_TEXTURE;
        frame.gl_texture = frames->gl_texture();
      } else if (!(frame.data = frames->host_pixels())) {
        return -1;
      }
      ready = true;
      return 0;
    }
    bool encode(uint64_t index, bool force_idr, std::vector<uint8_t> &bytes, bool &idr) override {
      if (!session || !ready) return false;
      agh_packet packet {}; packet.size = sizeof packet;
      char detail[1024] {};
      int result = library.encode(session, &frame, index, force_idr ? 1u : 0u, &packet, detail, sizeof detail);
      if (result == AGHL_EXTERNAL_UNAVAILABLE && frames) {
        // Nothing was encoded and the session is intact: the same frame again from host memory.
        BOOST_LOG(warning) << "[AURORA-GPU-HEVC] encoder cannot import GPU frames (" << detail << "); reading back";
        frames->drop_external();
        frame.flags = 0;
        frame.external_fd = -1;
        frame.gl_texture = 0;
        if (!(frame.data = frames->host_pixels())) return false;
        packet = {}; packet.size = sizeof packet;
        result = library.encode(session, &frame, index, force_idr ? 1u : 0u, &packet, detail, sizeof detail);
      }
      if (result || packet.frame_id != index || !packet.data || !packet.bytes) {
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
    std::string kernels, selector;
    std::vector<uint8_t> pixels;
    aghl_frame frame {};
    capture_format_e capture_format = capture_format_e::bgra8;
    bool ready = false;
    bool gpu_frames = false;  // the display hands over DMA-BUF descriptors (gpu-hevc-frames.h)
    std::string pci;
    std::unique_ptr<gpu_frames_t> frames;  // destroyed after the session (~device_t closes it first)
  };
}
  bool available() {
    // Hardware/driver changes require restarting the host. Discovery never
    // opens a screen-sharing prompt or starts a synthetic encode.
    static const bool usable = [] {
      std::string selector;
      try { selector = device_selector(); }
      catch (const std::exception &failure) {
        BOOST_LOG(warning) << "[AURORA-GPU-HEVC] " << failure.what(); return false;
      }
      std::error_code ec;
      for (const char *name : {"cabac.cl", "cabac_parallel.cl", "capture.cl", "coefficients.cl", "motion.cl",
                              "picture.cl", "prediction.cl", "residual.cl", "linux_capture.cl"})
        if (!std::filesystem::is_regular_file(directory / "kernels" / name, ec)) return false;
      for (unsigned depth : {8u, 10u}) {
        library_t lib;
        if (!lib.load(depth)) return false;
        char name[512] {}, error[1024] {};
        if (lib.probe(selector.c_str(), name, sizeof name, error, sizeof error)) {
          BOOST_LOG(warning) << "[AURORA-GPU-HEVC] unavailable: " << error;
          return false;
        }
      }
      return true;
    }();
    return usable;
  }
  std::unique_ptr<platf::gpu_hevc_encode_device_t> make_device(const video::config_t &config, bool hdr, capture_format_e format, bool gpu_frames) {
    auto device = std::make_unique<device_t>();
    if (!device->init(config, hdr, format, gpu_frames)) return {};
    return device;
  }
}
