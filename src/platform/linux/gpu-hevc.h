// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include "src/platform/common.h"
namespace video { struct config_t; }
namespace platf { struct gpu_hevc_encode_device_t; }
namespace gpu_hevc {
  enum class capture_format_e { bgra8, bt2020_pq_xbgr2101010 };
  struct cpu_image_t: platf::img_t { capture_format_e capture_format = capture_format_e::bgra8; };
  bool available();
  // gpu_frames: the display hands over captured DMA-BUFs (egl::img_descriptor_t), staged on the GPU
  // by gpu-hevc-frames.h instead of CPU pixels.
  std::unique_ptr<platf::gpu_hevc_encode_device_t> make_device(const video::config_t &config, bool hdr,
    capture_format_e format = capture_format_e::bgra8, bool gpu_frames = false);
}
