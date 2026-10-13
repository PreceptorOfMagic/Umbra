// SPDX-License-Identifier: GPL-3.0-only
#include "gpu-hevc-frames.h"

#include <ffnvcodec/dynlink_cuda.h>

#include "graphics.h"
#include "src/logging.h"
#include "src/utility.h"

#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <drm_fourcc.h>
#include <limits>
#include <type_traits>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace gpu_hevc {
namespace {
  std::atomic<bool> stage_broken {false}, external_broken {false};

  // CUDA virtual memory management (driver API since CUDA 10.2), declared here because the
  // dynlink headers stop short of it; layouts and values are those of the toolkit's cuda.h.
  using CUmemGenericAllocationHandle = unsigned long long;
  struct cu_mem_location_t { int type; int id; };
  struct cu_mem_allocation_prop_t {
    int type;
    int requestedHandleTypes;
    cu_mem_location_t location;
    void *win32HandleMetaData;
    struct { unsigned char compressionType, gpuDirectRDMACapable; unsigned short usage; unsigned char reserved[4]; } allocFlags;
  };
  struct cu_mem_access_desc_t { cu_mem_location_t location; int flags; };
  constexpr int CU_MEM_ALLOCATION_TYPE_PINNED_ = 1, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_ = 1,
                CU_MEM_LOCATION_TYPE_DEVICE_ = 1, CU_MEM_ACCESS_FLAGS_PROT_READWRITE_ = 3, CU_MEM_ALLOC_GRANULARITY_MINIMUM_ = 0;

  struct cuda_api_t {
    void *module = nullptr;
    CUresult (*init)(unsigned) = nullptr;
    CUresult (*device_by_pci)(CUdevice *, const char *) = nullptr;
    CUresult (*retain)(CUcontext *, CUdevice) = nullptr;
    CUresult (*release)(CUdevice) = nullptr;
    CUresult (*push)(CUcontext) = nullptr;
    CUresult (*pop)(CUcontext *) = nullptr;
    CUresult (*gl_register)(CUgraphicsResource *, GLuint, GLenum, unsigned) = nullptr;
    CUresult (*unregister)(CUgraphicsResource) = nullptr;
    CUresult (*map)(unsigned, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*unmap)(unsigned, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*mapped_array)(CUarray *, CUgraphicsResource, unsigned, unsigned) = nullptr;
    CUresult (*copy2d)(const CUDA_MEMCPY2D *) = nullptr;
    CUresult (*synchronize)() = nullptr;
    CUresult (*granularity)(size_t *, const cu_mem_allocation_prop_t *, int) = nullptr;
    CUresult (*create)(CUmemGenericAllocationHandle *, size_t, const cu_mem_allocation_prop_t *, unsigned long long) = nullptr;
    CUresult (*release_handle)(CUmemGenericAllocationHandle) = nullptr;
    CUresult (*reserve)(CUdeviceptr *, size_t, size_t, CUdeviceptr, unsigned long long) = nullptr;
    CUresult (*free_address)(CUdeviceptr, size_t) = nullptr;
    CUresult (*map_memory)(CUdeviceptr, size_t, size_t, CUmemGenericAllocationHandle, unsigned long long) = nullptr;
    CUresult (*unmap_memory)(CUdeviceptr, size_t) = nullptr;
    CUresult (*set_access)(CUdeviceptr, size_t, const cu_mem_access_desc_t *, size_t) = nullptr;
    CUresult (*export_handle)(void *, CUmemGenericAllocationHandle, int, unsigned long long) = nullptr;
    CUresult (*error_name)(CUresult, const char **) = nullptr;

    bool load() {
      if (module) return true;
      module = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
      if (!module) return false;
      bool ok = true;
      auto get = [&](auto &fn, const char *name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(module, name));
        ok = ok && fn;
      };
      get(init, "cuInit"); get(device_by_pci, "cuDeviceGetByPCIBusId"); get(retain, "cuDevicePrimaryCtxRetain");
      get(release, "cuDevicePrimaryCtxRelease_v2"); get(push, "cuCtxPushCurrent_v2"); get(pop, "cuCtxPopCurrent_v2");
      get(gl_register, "cuGraphicsGLRegisterImage"); get(unregister, "cuGraphicsUnregisterResource");
      get(map, "cuGraphicsMapResources"); get(unmap, "cuGraphicsUnmapResources");
      get(mapped_array, "cuGraphicsSubResourceGetMappedArray"); get(copy2d, "cuMemcpy2D_v2");
      get(synchronize, "cuCtxSynchronize"); get(granularity, "cuMemGetAllocationGranularity");
      get(create, "cuMemCreate"); get(release_handle, "cuMemRelease"); get(reserve, "cuMemAddressReserve");
      get(free_address, "cuMemAddressFree"); get(map_memory, "cuMemMap"); get(unmap_memory, "cuMemUnmap");
      get(set_access, "cuMemSetAccess"); get(export_handle, "cuMemExportToShareableHandle"); get(error_name, "cuGetErrorName");
      if (!ok) { dlclose(module); module = nullptr; }
      return ok;
    }
    std::string name(CUresult result) const {
      const char *text = nullptr;
      if (error_name) error_name(result, &text);
      return text ? text : std::to_string(int(result));
    }
  };
  cuda_api_t &cuda() {
    static cuda_api_t api;
    return api;
  }

  // One full-screen triangle; texelFetch keeps every pixel exact (no filtering, no scaling).
  // UNORM texels fetch as exactly k/(2^n-1), so round() recovers k bit-exactly.
  constexpr const char *vertex_source = "#version 330 core\n"
    "void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
  constexpr const char *pack_bgra8_source = "#version 330 core\n"
    "uniform sampler2D source; out uint word;\n"
    "void main() { uvec4 v = uvec4(round(texelFetch(source, ivec2(gl_FragCoord.xy), 0) * 255.0));\n"
    "  word = v.b | (v.g << 8) | (v.r << 16) | (255u << 24); }\n";
  constexpr const char *pack_pq_source = "#version 330 core\n"
    "uniform sampler2D source; out uint word;\n"
    "void main() { uvec3 v = uvec3(round(texelFetch(source, ivec2(gl_FragCoord.xy), 0).rgb * 1023.0));\n"
    "  word = v.r | (v.g << 10) | (v.b << 20) | (3u << 30); }\n";

  class frames_t final: public gpu_frames_t {
  public:
    ~frames_t() override {
      if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT &&
          eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        drop_cuda();
        if (program) gl.DeleteProgram(program);
        if (vao) gl.DeleteVertexArrays(1, &vao);
        if (framebuffer) gl.DeleteFramebuffers(1, &framebuffer);
        if (packed) gl.DeleteTextures(1, &packed);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
      if (display != EGL_NO_DISPLAY) {
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        eglTerminate(display);
      }
      if (wayland) wl_display_disconnect(wayland);
    }

    bool init(int w, int h, bool is_pq, const std::string &pci) {
      width = w;
      height = h;
      pq = is_pq;
      pixels.resize(bytes());
      // The compositor's GPU, as the capture's DMA-BUF reader uses (portal-dmabuf.h).
      if (!eglGetPlatformDisplay || !eglCreateContext || !eglMakeCurrent) return fail("EGL unavailable");
      wayland = wl_display_connect(nullptr);
      if (!wayland) return fail("no Wayland display");
      display = eglGetPlatformDisplay(0x31D8 /* EGL_PLATFORM_WAYLAND_KHR */, wayland, nullptr);
      if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_API))
        return fail("EGL initialization failed");
      const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
      if (!extensions || !std::strstr(extensions, "EGL_EXT_image_dma_buf_import_modifiers") ||
          !std::strstr(extensions, "EGL_KHR_surfaceless_context")) return fail("EGL lacks DMA-BUF import or surfaceless contexts");
      const EGLint config_attrs[] {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
      EGLConfig config; EGLint count = 0;
      if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || !count) return fail("no OpenGL EGL config");
      const EGLint context_attrs[] {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
      context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
      if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return fail("no OpenGL 3.3 core context");
      auto release = util::fail_guard([&] { eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
      if (!gladLoadGLContext(&gl, eglGetProcAddress) || !gl.EGLImageTargetTexture2DOES || !gl.GetTexImage || !gl.TexStorage2D)
        return fail("OpenGL entry points missing");
      create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
      destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
      if (!create_image || !destroy_image) return fail("EGLImage entry points missing");
      if (auto renderer = gl.GetString(GL_RENDERER)) gpu = reinterpret_cast<const char *>(renderer);

      gl.GenTextures(1, &packed);
      gl.BindTexture(GL_TEXTURE_2D, packed);
      gl.TexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, width, height);
      gl.GenFramebuffers(1, &framebuffer);
      gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, packed, 0);
      if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return fail("packed framebuffer incomplete");
      program = link(pq ? pack_pq_source : pack_bgra8_source);
      if (!program) return false;
      gl.GenVertexArrays(1, &vao);
      if (gl.GetError() != GL_NO_ERROR) return fail("OpenGL setup error");
      if (!pci.empty() && !external_broken) start_cuda(pci);
      BOOST_LOG(info) << "[AURORA-GPU-HEVC] GPU frames " << width << 'x' << height << (pq ? " PQ" : " BGRA8")
        << " on " << gpu << ", hand-off=" << hand_off() << (handoff_detail.empty() ? "" : " (" + handoff_detail + ')');
      return true;
    }

    bool stage(const egl::surface_descriptor_t *frame) override {
      if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return fail("context not current");
      auto release = util::fail_guard([&] { eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
      gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      if (!frame) {
        const GLuint black[] {0, 0, 0, 0};
        gl.ClearBufferuiv(GL_COLOR, 0, black);
      } else {
        if (frame->width != width || frame->height != height) return false;  // caller recreates at the new size
        auto attrs = attributes(*frame);
        auto image = create_image(display, EGL_NO_CONTEXT, 0x3270 /* EGL_LINUX_DMA_BUF_EXT */, nullptr, attrs.data());
        if (!image) return fail("DMA-BUF import failed");
        auto release_image = util::fail_guard([&] { destroy_image(display, image); });
        GLuint source = 0;
        gl.GenTextures(1, &source);
        auto release_source = util::fail_guard([&] { gl.DeleteTextures(1, &source); });
        gl.BindTexture(GL_TEXTURE_2D, source);
        gl.EGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.Viewport(0, 0, width, height);
        gl.UseProgram(program);
        gl.BindVertexArray(vao);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        if (gl.GetError() != GL_NO_ERROR) return fail("pack draw failed");
      }
      host_valid = false;
      if (resource && !copy_to_export()) {
        // The draw stands; only the hand-off changes.
        BOOST_LOG(warning) << "[AURORA-GPU-HEVC] CUDA hand-off failed (" << handoff_detail << "); reading back instead";
        drop_external();
      }
      if (!resource) gl.Flush();
      return true;
    }

    int external_fd() const override { return resource ? export_fd : -1; }
    size_t bytes() const override { return size_t(width) * height * 4; }

    const uint8_t *host_pixels() override {
      if (host_valid) return pixels.data();
      if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return nullptr;
      auto release = util::fail_guard([&] { eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
      gl.BindTexture(GL_TEXTURE_2D, packed);
      gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
      gl.GetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, pixels.data());
      if (gl.GetError() != GL_NO_ERROR) return nullptr;
      host_valid = true;
      return pixels.data();
    }

    void drop_external() override {
      external_broken = true;
      if (!resource) return;
      if (eglGetCurrentContext() == context || eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) drop_cuda();
    }

    const char *hand_off() const override { return resource ? "cuda" : "readback"; }

  private:
    bool fail(const std::string &what) {
      BOOST_LOG(error) << "[AURORA-GPU-HEVC] GPU frames: " << what;
      stage_broken = true;
      return false;
    }

    std::vector<EGLint> attributes(const egl::surface_descriptor_t &sd) const {
      constexpr EGLint fd_attrs[] {0x3272, 0x3275, 0x3278, 0x3440};
      constexpr EGLint offset_attrs[] {0x3273, 0x3276, 0x3279, 0x3441};
      constexpr EGLint pitch_attrs[] {0x3274, 0x3277, 0x327A, 0x3442};
      constexpr EGLint lo_attrs[] {0x3443, 0x3445, 0x3447, 0x3449};
      constexpr EGLint hi_attrs[] {0x3444, 0x3446, 0x3448, 0x344A};
      std::vector<EGLint> attrs {EGL_WIDTH, sd.width, EGL_HEIGHT, sd.height, 0x3271 /* EGL_LINUX_DRM_FOURCC_EXT */, EGLint(sd.fourcc)};
      for (int i = 0; i < 4 && sd.fds[i] >= 0; ++i) {
        attrs.insert(attrs.end(), {fd_attrs[i], sd.fds[i], offset_attrs[i], EGLint(sd.offsets[i]), pitch_attrs[i], EGLint(sd.pitches[i])});
        if (sd.modifier != DRM_FORMAT_MOD_INVALID)
          attrs.insert(attrs.end(), {lo_attrs[i], EGLint(sd.modifier & 0xffffffffu), hi_attrs[i], EGLint(sd.modifier >> 32)});
      }
      attrs.push_back(EGL_NONE);
      return attrs;
    }

    GLuint link(const char *fragment_source) {
      auto compile = [&](GLenum type, const char *source) -> GLuint {
        GLuint shader = gl.CreateShader(type);
        gl.ShaderSource(shader, 1, &source, nullptr);
        gl.CompileShader(shader);
        GLint ok = 0;
        gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
          char log[512] {};
          gl.GetShaderInfoLog(shader, sizeof log, nullptr, log);
          fail(std::string("pack shader: ") + log);
          gl.DeleteShader(shader);
          return 0;
        }
        return shader;
      };
      GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source);
      GLuint fragment = vertex ? compile(GL_FRAGMENT_SHADER, fragment_source) : 0;
      if (!fragment) { if (vertex) gl.DeleteShader(vertex); return 0; }
      GLuint linked = gl.CreateProgram();
      gl.AttachShader(linked, vertex);
      gl.AttachShader(linked, fragment);
      gl.LinkProgram(linked);
      gl.DeleteShader(vertex);
      gl.DeleteShader(fragment);
      GLint ok = 0;
      gl.GetProgramiv(linked, GL_LINK_STATUS, &ok);
      if (!ok) { gl.DeleteProgram(linked); fail("pack program link failed"); return 0; }
      return linked;
    }

    // NVIDIA: the packed texture is copied by CUDA into a VMM allocation exported as an opaque fd.
    void start_cuda(const std::string &pci) {
      auto &cu = cuda();
      if (!cu.load()) { handoff_detail = "no CUDA driver"; return; }
      CUresult r;
      if ((r = cu.init(0))) { handoff_detail = "cuInit " + cu.name(r); return; }
      if ((r = cu.device_by_pci(&device, pci.c_str()))) { handoff_detail = "no CUDA device at " + pci; return; }
      if ((r = cu.retain(&cuda_context, device))) { handoff_detail = "context " + cu.name(r); return; }
      retained = true;
      if ((r = cu.push(cuda_context))) { handoff_detail = "context push " + cu.name(r); drop_cuda(); return; }
      std::string error = setup_cuda(pci);
      CUcontext popped;
      cu.pop(&popped);
      if (!error.empty()) {
        handoff_detail = error;
        drop_cuda();
      }
    }

    // With the CUDA context pushed; "" on success.
    std::string setup_cuda(const std::string &pci) {
      auto &cu = cuda();
      CUresult r;
      auto failed = [&](const std::string &what, CUresult result) { return what + ' ' + cu.name(result); };
      cu_mem_allocation_prop_t prop {};
      prop.type = CU_MEM_ALLOCATION_TYPE_PINNED_;
      prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_;
      prop.location = {CU_MEM_LOCATION_TYPE_DEVICE_, int(device)};
      size_t granularity = 0;
      if ((r = cu.granularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM_)) || !granularity)
        return failed("granularity", r);
      allocation_size = (bytes() + granularity - 1) / granularity * granularity;
      if ((r = cu.create(&allocation, allocation_size, &prop, 0))) return failed("cuMemCreate", r);
      have_allocation = true;
      if ((r = cu.reserve(&address, allocation_size, 0, 0, 0))) return failed("reserve", r);
      if ((r = cu.map_memory(address, allocation_size, 0, allocation, 0))) return failed("map", r);
      mapped = true;
      const cu_mem_access_desc_t access {{CU_MEM_LOCATION_TYPE_DEVICE_, int(device)}, CU_MEM_ACCESS_FLAGS_PROT_READWRITE_};
      if ((r = cu.set_access(address, allocation_size, &access, 1))) return failed("access", r);
      if ((r = cu.export_handle(&export_fd, allocation, CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_, 0)) || export_fd < 0)
        return failed("export", r);
      // Needs the GL context current and the GL and CUDA devices to be the same GPU.
      if ((r = cu.gl_register(&resource, packed, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY))) {
        resource = nullptr;
        return failed("GL interop (encoder GPU " + pci + " is not the compositor's?)", r);
      }
      return {};
    }

    bool copy_to_export() {
      auto &cu = cuda();
      CUresult r;
      if ((r = cu.push(cuda_context))) { handoff_detail = "context push " + cu.name(r); return false; }
      CUcontext popped;
      auto pop = util::fail_guard([&] { cu.pop(&popped); });
      // Mapping orders CUDA after the GL draw (the interop contract).
      if ((r = cu.map(1, &resource, nullptr))) { handoff_detail = "map " + cu.name(r); return false; }
      CUDA_MEMCPY2D copy {};
      copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
      if ((r = cu.mapped_array(&copy.srcArray, resource, 0, 0))) {
        cu.unmap(1, &resource, nullptr);
        handoff_detail = "mapped array " + cu.name(r);
        return false;
      }
      copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      copy.dstDevice = address;
      copy.dstPitch = size_t(width) * 4;
      copy.WidthInBytes = size_t(width) * 4;
      copy.Height = height;
      r = cu.copy2d(&copy);
      cu.unmap(1, &resource, nullptr);
      // Complete before the encoder's OpenCL queue reads the allocation.
      if (r || (r = cu.synchronize())) { handoff_detail = "copy " + cu.name(r); return false; }
      return true;
    }

    void drop_cuda() {
      auto &cu = cuda();
      if (!cu.module) return;
      CUcontext popped;
      const bool pushed = retained && !cu.push(cuda_context);
      if (resource) { cu.unregister(resource); resource = nullptr; }
      if (mapped) { cu.unmap_memory(address, allocation_size); mapped = false; }
      if (address) { cu.free_address(address, allocation_size); address = 0; }
      if (have_allocation) { cu.release_handle(allocation); have_allocation = false; }
      if (pushed) cu.pop(&popped);
      if (export_fd >= 0) { close(export_fd); export_fd = -1; }
      if (retained) { cu.release(device); retained = false; }
    }

    int width = 0, height = 0;
    bool pq = false, host_valid = false;
    std::string gpu, handoff_detail;
    std::vector<uint8_t> pixels;
    wl_display *wayland = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    GladGLContext gl {};
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    GLuint packed = 0, framebuffer = 0, program = 0, vao = 0;
    // CUDA hand-off
    CUdevice device = 0;
    CUcontext cuda_context = nullptr;
    bool retained = false, have_allocation = false, mapped = false;
    CUmemGenericAllocationHandle allocation = 0;
    CUdeviceptr address = 0;
    size_t allocation_size = 0;
    int export_fd = -1;
    CUgraphicsResource resource = nullptr;
  };
}  // namespace

  std::unique_ptr<gpu_frames_t> gpu_frames_t::create(int width, int height, bool pq, const std::string &pci) {
    if (width < 1 || height < 1 || width > 8192 || height > 8192) return nullptr;
    auto frames = std::make_unique<frames_t>();
    if (!frames->init(width, height, pq, pci)) return nullptr;
    return frames;
  }

  bool gpu_frames_possible() {
    return !stage_broken;
  }
}  // namespace gpu_hevc
