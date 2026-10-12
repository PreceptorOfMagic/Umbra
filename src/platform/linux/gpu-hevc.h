// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
namespace video { struct config_t; }
namespace platf { struct gpu_hevc_encode_device_t; }
namespace gpu_hevc {
  bool available();
  std::unique_ptr<platf::gpu_hevc_encode_device_t> make_device(const video::config_t &config, bool hdr);
}
