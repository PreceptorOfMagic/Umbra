// SPDX-License-Identifier: GPL-3.0-only
// GPU frames for the GPU HEVC encoder, as Windows hands it the captured D3D11 texture. A captured
// DMA-BUF is imported on the compositor's GPU (EGL) and one GL draw packs it into the encoder's
// input words (BGRA8, or xBGR2101010 PQ) in a texture owned here. Hand-off to the encoder:
//  - cuda: CUDA copies that texture into a CUDA VMM allocation exported as an opaque fd, which the
//    OpenCL encoder imports (cl_khr_external_memory_opaque_fd). The frame never visits host memory.
//    Measured on VEGA (RTX 2060 SUPER, 595.99.02): NVIDIA OpenCL has no DMA-BUF import and refuses
//    EGL/GL sharing; CUDA registers neither a DMA-BUF EGLImage nor a texture bound to one
//    (CUDA_ERROR_INVALID_VALUE); the draw + CUDA copy is bit-exact and took 0.46 ms at 1920x2176,
//    0.62 ms at 3840x2160 (gpu-linux/bridge-probe.c).
//  - gl-sharing: the encoder's OpenCL context shares this EGL context and reads the packed texture
//    (cl_khr_gl_sharing). Mesa rusticl (AMD/Intel in the Flatpak runtime) has no external-memory
//    import but has this; the sequence was verified bit-exact on rusticl+zink (gpu-linux/glshare-probe.c).
//  - readback: the packed texture is read to host memory (any GPU; what the capture did before).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace egl { struct surface_descriptor_t; }

namespace gpu_hevc {
  class gpu_frames_t {
  public:
    // pci: "dddd:bb:dd.f" of the encoder's GPU; empty allows only the read-back hand-off.
    static std::unique_ptr<gpu_frames_t> create(int width, int height, bool pq, const std::string &pci);
    virtual ~gpu_frames_t() = default;
    // New capture size: a new packed texture (and CUDA export) in the same GL context.
    virtual bool resize(int width, int height) = 0;
    virtual int frame_width() const = 0;
    virtual int frame_height() const = 0;
    // Pack one captured frame; nullptr packs black (the encoder's dummy frame before the first capture).
    virtual bool stage(const egl::surface_descriptor_t *frame) = 0;
    // After stage(): the exported allocation's fd (cuda), rows packed at width*4 bytes, else -1.
    virtual int external_fd() const = 0;
    // After stage(): the packed GL_R32UI texture (gl-sharing), else 0. Its GL work is finished.
    virtual unsigned gl_texture() const = 0;
    // Changes whenever the texture or export is replaced (aghl_frame.external_serial).
    virtual uint64_t serial() const = 0;
    // The encoder's OpenCL context shares this EGL context (cl_khr_gl_sharing, e.g. Mesa rusticl on
    // AMD): hand the packed texture over directly. Ignored while the CUDA hand-off is active.
    virtual void use_gl_sharing() = 0;
    virtual void *egl_display() const = 0;
    virtual void *egl_context() const = 0;
    virtual size_t bytes() const = 0;
    // The staged frame in host memory (read on demand); nullptr on failure.
    virtual const uint8_t *host_pixels() = 0;
    // The encoder could not import the export or texture: read back from now on, here and in later sessions.
    virtual void drop_external() = 0;
    virtual const char *hand_off() const = 0;
  };
  // False once a GL stage failed: later GPU HEVC captures read back in the capture instead.
  bool gpu_frames_possible();
}  // namespace gpu_hevc
