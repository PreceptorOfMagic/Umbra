/**
 * @file src/nvenc/nvenc_cuda.h
 * @brief Declarations for the Linux NVENC encoder fed from a CUDA surface.
 */
#pragma once

#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)

  // standard includes
  #include <cstddef>

  // lib includes
  #include <ffnvcodec/dynlink_loader.h>

  // local includes
  #include "nvenc_base.h"

namespace nvenc {

  /**
   * @brief Umbra: the encoder Windows Umbra uses (nvenc_base), on Linux.
   *
   * FFmpeg's hevc_nvenc never sets the per-picture constrainedFrame flag and ignores the client's
   * intra-refresh request, so a Linux co-op pane spliced beside a Windows one drifted out from the
   * seam and never healed. nvenc_base does both, exactly as on Windows. The platform device
   * (platform/linux/cuda.cpp) converts each frame into input_buffer() on `stream`.
   *
   * The CUDA context, function table and stream belong to the platform device and must outlive this.
   */
  class nvenc_cuda final: public nvenc_base {
  public:
    nvenc_cuda(CUcontext cuda_context, CudaFunctions *cuda_functions, CUstream stream);
    ~nvenc_cuda() override;

    CUdeviceptr input_buffer() const {
      return cuda_input_buffer;
    }

    std::size_t input_pitch() const {
      return cuda_input_pitch;
    }

  private:
    bool init_library() override;
    bool create_and_register_input_buffer() override;
    bool synchronize_input_buffer() override;

    bool push_context();
    void pop_context();

    CUcontext cuda_context;
    CudaFunctions *cuda_functions;
    CUstream stream;
    CUdeviceptr cuda_input_buffer = 0;
    std::size_t cuda_input_pitch = 0;
    void *library = nullptr;
  };

}  // namespace nvenc

#endif
