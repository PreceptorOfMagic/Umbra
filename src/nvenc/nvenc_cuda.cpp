/**
 * @file src/nvenc/nvenc_cuda.cpp
 * @brief Definitions for the Linux NVENC encoder fed from a CUDA surface.
 */
// local includes
#include "src/logging.h"

#if defined(__linux__) && defined(SUNSHINE_BUILD_CUDA)
  #include "nvenc_cuda.h"

  // standard includes
  #include <dlfcn.h>
  #include <memory>

namespace nvenc {

  nvenc_cuda::nvenc_cuda(CUcontext cuda_context, CudaFunctions *cuda_functions, CUstream stream):
      nvenc_base(NV_ENC_DEVICE_TYPE_CUDA),
      cuda_context(cuda_context),
      cuda_functions(cuda_functions),
      stream(stream) {
    device = cuda_context;
  }

  nvenc_cuda::~nvenc_cuda() {
    if (encoder) {
      destroy_encoder();
    }

    if (cuda_input_buffer && push_context()) {
      if (cuda_functions->cuMemFree(cuda_input_buffer) != CUDA_SUCCESS) {
        BOOST_LOG(error) << "NvEnc: couldn't free the CUDA input surface";
      }
      pop_context();
    }
    cuda_input_buffer = 0;

    if (library) {
      dlclose(library);
      library = nullptr;
    }
  }

  bool nvenc_cuda::push_context() {
    if (cuda_functions->cuCtxPushCurrent(cuda_context) != CUDA_SUCCESS) {
      BOOST_LOG(error) << "NvEnc: couldn't make the CUDA context current";
      return false;
    }
    return true;
  }

  void nvenc_cuda::pop_context() {
    CUcontext popped;
    cuda_functions->cuCtxPopCurrent(&popped);
  }

  bool nvenc_cuda::init_library() {
    if (library) {
      return true;
    }

    // The same library FFmpeg's nvenc loads; the Flatpak gets it from the NVIDIA GL extension.
    constexpr auto library_name = "libnvidia-encode.so.1";
    if ((library = dlopen(library_name, RTLD_LAZY | RTLD_LOCAL))) {
      if (auto create_instance = (decltype(NvEncodeAPICreateInstance) *) dlsym(library, "NvEncodeAPICreateInstance")) {
        auto new_nvenc = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
        new_nvenc->version = min_struct_version(NV_ENCODE_API_FUNCTION_LIST_VER);
        if (nvenc_failed(create_instance(new_nvenc.get()))) {
          BOOST_LOG(error) << "NvEnc: NvEncodeAPICreateInstance() failed: " << last_nvenc_error_string;
        } else {
          nvenc = std::move(new_nvenc);
          return true;
        }
      } else {
        BOOST_LOG(error) << "NvEnc: No NvEncodeAPICreateInstance() in " << library_name;
      }
    } else {
      BOOST_LOG(debug) << "NvEnc: Couldn't load NvEnc library " << library_name << ": " << dlerror();
    }

    if (library) {
      dlclose(library);
      library = nullptr;
    }

    return false;
  }

  bool nvenc_cuda::create_and_register_input_buffer() {
    std::size_t bytes_per_sample;
    switch (encoder_params.buffer_format) {
      case NV_ENC_BUFFER_FORMAT_NV12:
        bytes_per_sample = 1;
        break;
      case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
        bytes_per_sample = 2;
        break;
      default:
        BOOST_LOG(error) << "NvEnc: unsupported CUDA input format " << (int) encoder_params.buffer_format;
        return false;
    }

    if (!cuda_input_buffer) {
      if (!push_context()) {
        return false;
      }
      // Luma rows, then the interleaved chroma plane at pitch * height.
      auto result = cuda_functions->cuMemAllocPitch(&cuda_input_buffer, &cuda_input_pitch, encoder_params.width * bytes_per_sample, encoder_params.height + encoder_params.height / 2, 16);
      pop_context();
      if (result != CUDA_SUCCESS) {
        BOOST_LOG(error) << "NvEnc: cuMemAllocPitch() failed: error " << (int) result;
        cuda_input_buffer = 0;
        return false;
      }
    }

    if (!registered_input_buffer) {
      NV_ENC_REGISTER_RESOURCE register_resource = {min_struct_version(NV_ENC_REGISTER_RESOURCE_VER, 3, 4)};
      register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
      register_resource.width = encoder_params.width;
      register_resource.height = encoder_params.height;
      register_resource.pitch = cuda_input_pitch;
      register_resource.resourceToRegister = (void *) cuda_input_buffer;
      register_resource.bufferFormat = encoder_params.buffer_format;
      register_resource.bufferUsage = NV_ENC_INPUT_IMAGE;

      if (nvenc_failed(nvenc->nvEncRegisterResource(encoder, &register_resource))) {
        BOOST_LOG(error) << "NvEnc: NvEncRegisterResource() failed: " << last_nvenc_error_string;
        return false;
      }

      registered_input_buffer = register_resource.registeredResource;
    }

    return true;
  }

  bool nvenc_cuda::synchronize_input_buffer() {
    // The platform device queues the conversion copies on `stream`; NVENC must not read the
    // surface before they land.
    if (!push_context()) {
      return false;
    }
    auto result = cuda_functions->cuStreamSynchronize(stream);
    pop_context();
    if (result != CUDA_SUCCESS) {
      BOOST_LOG(error) << "NvEnc: cuStreamSynchronize() failed: error " << (int) result;
      return false;
    }
    return true;
  }

}  // namespace nvenc
#endif
