// Leg test for the co-op zero-copy bridge: DMA-BUF -> CUDA -> linear device copy, by several routes.
// A GBM buffer is painted four known colours in GL (one per quadrant), exported as a DMA-BUF
// exactly as a compositor hands one over, re-imported, copied by CUDA and read back; the copied
// bytes must be the DRM format's memory order (XRGB8888 = B,G,R,X; XBGR2101010 = R|G<<10|B<<20).
// Routes (argv[5]): egl     cuGraphicsEGLRegisterImage on the DMA-BUF EGLImage
//                   egltex  the same on an EGLImage made from the imported GL texture
//                   gl      cuGraphicsGLRegisterImage on the imported texture itself
//                   draw    GL draw (swizzle to memory order) into an owned texture registered once
// Build: gcc -O2 -I<cuda include> bridge-probe.c -o bridge-probe -lEGL -lOpenGL -lgbm -ldl
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <cuda.h>
#include <cudaEGL.h>
#include <cudaGL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOAD(name, sym) name = dlsym(cu, sym); if (!name) { printf("FAIL dlsym %s\n", sym); return 1; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
typedef void (*target_fn)(GLenum, void *);

static GLuint shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, NULL); glCompileShader(s);
  GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); printf("FAIL shader %s\n", log); exit(1); }
  return s;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
  int w = argc > 2 ? atoi(argv[2]) : 1920, h = argc > 3 ? atoi(argv[3]) : 2176;
  int ten = argc > 4 && !strcmp(argv[4], "10");
  const char *route = argc > 5 ? argv[5] : "egl";
  unsigned flags = argc > 6 ? (unsigned) atoi(argv[6]) : 0;
  uint32_t fourcc = ten ? GBM_FORMAT_XBGR2101010 : GBM_FORMAT_XRGB8888;
  int drm = open(node, O_RDWR | O_CLOEXEC);
  struct gbm_device *gbm = drm >= 0 ? gbm_create_device(drm) : NULL;
  if (!gbm) { printf("FAIL gbm %s\n", node); return 1; }
  PFNEGLGETPLATFORMDISPLAYEXTPROC getdisp = (void *) eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = getdisp(EGL_PLATFORM_GBM_KHR, gbm, NULL);
  EGLint major, minor;
  if (!eglInitialize(dpy, &major, &minor)) { printf("FAIL eglInitialize 0x%x\n", eglGetError()); return 1; }
  eglBindAPI(EGL_OPENGL_API);
  EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
  if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("FAIL ctx 0x%x\n", eglGetError()); return 1; }
  printf("EGL %d.%d vendor=%s GL=%s route=%s flags=%u\n", major, minor, eglQueryString(dpy, EGL_VENDOR),
         (const char *) glGetString(GL_VERSION), route, flags);
  PFNEGLCREATEIMAGEKHRPROC create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
  target_fn target = (target_fn) eglGetProcAddress("glEGLImageTargetTexture2DOES");

  struct gbm_bo *bo = gbm_bo_create(gbm, w, h, fourcc, GBM_BO_USE_RENDERING);
  if (!bo) { printf("FAIL gbm_bo_create\n"); return 1; }
  int fd = gbm_bo_get_fd(bo);
  uint64_t modifier = gbm_bo_get_modifier(bo);
  uint32_t stride = gbm_bo_get_stride(bo), offset = gbm_bo_get_offset(bo, 0);
  printf("bo %dx%d fourcc=%.4s modifier=0x%llx stride=%u\n", w, h, (char *) &fourcc, (unsigned long long) modifier, stride);
  EGLint attrs[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, (EGLint) fourcc,
    EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint) offset,
    EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint) stride,
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint) (modifier & 0xffffffff),
    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint) (modifier >> 32), EGL_NONE};
  // Paint through one import (the "compositor"), copy through a second one (the "capture").
  EGLImageKHR paint = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
  if (!paint) { printf("FAIL paint import 0x%x\n", eglGetError()); return 1; }
  GLuint tex, fbo;
  glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); target(GL_TEXTURE_2D, paint);
  glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FAIL fbo\n"); return 1; }
  const float colours[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
  glEnable(GL_SCISSOR_TEST);
  for (int q = 0; q < 4; ++q) {
    // GL's origin is bottom-left; quadrant q: 0=top-left 1=top-right 2=bottom-left 3=bottom-right.
    glScissor((q & 1) * w / 2, (q & 2) ? 0 : h / 2, w / 2, h / 2);
    glClearColor(colours[q][0], colours[q][1], colours[q][2], 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glDisable(GL_SCISSOR_TEST);
  glFinish();
  EGLImageKHR image = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
  if (!image) { printf("FAIL capture import 0x%x\n", eglGetError()); return 1; }
  GLuint captex; glGenTextures(1, &captex); glBindTexture(GL_TEXTURE_2D, captex); target(GL_TEXTURE_2D, image);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  if (glGetError()) { printf("FAIL capture texture\n"); return 1; }

  void *cu = dlopen("libcuda.so.1", RTLD_NOW);
  if (!cu) { printf("FAIL libcuda\n"); return 1; }
  CUresult (*init)(unsigned); CUresult (*bypci)(CUdevice *, const char *);
  CUresult (*retain)(CUcontext *, CUdevice); CUresult (*push)(CUcontext);
  CUresult (*reg)(CUgraphicsResource *, EGLImageKHR, unsigned); CUresult (*unreg)(CUgraphicsResource);
  CUresult (*glreg)(CUgraphicsResource *, GLuint, GLenum, unsigned);
  CUresult (*map)(unsigned, CUgraphicsResource *, CUstream); CUresult (*unmap)(unsigned, CUgraphicsResource *, CUstream);
  CUresult (*getarray)(CUarray *, CUgraphicsResource, unsigned, unsigned);
  CUresult (*getframe)(CUeglFrame *, CUgraphicsResource, unsigned, unsigned);
  CUresult (*alloc)(CUdeviceptr *, size_t); CUresult (*copy2d)(const CUDA_MEMCPY2D *);
  CUresult (*dtoh)(void *, CUdeviceptr, size_t); CUresult (*sync)(void);
  CUresult (*errname)(CUresult, const char **);
  LOAD(init, "cuInit") LOAD(bypci, "cuDeviceGetByPCIBusId") LOAD(retain, "cuDevicePrimaryCtxRetain")
  LOAD(push, "cuCtxPushCurrent_v2") LOAD(reg, "cuGraphicsEGLRegisterImage") LOAD(unreg, "cuGraphicsUnregisterResource")
  LOAD(glreg, "cuGraphicsGLRegisterImage") LOAD(map, "cuGraphicsMapResources") LOAD(unmap, "cuGraphicsUnmapResources")
  LOAD(getarray, "cuGraphicsSubResourceGetMappedArray")
  LOAD(getframe, "cuGraphicsResourceGetMappedEglFrame") LOAD(alloc, "cuMemAlloc_v2") LOAD(copy2d, "cuMemcpy2D_v2")
  LOAD(dtoh, "cuMemcpyDtoH_v2") LOAD(sync, "cuCtxSynchronize") LOAD(errname, "cuGetErrorName")
  char pci[64] = {0};
  { char link[256]; snprintf(link, sizeof link, "/sys/class/drm/%s/device", strrchr(node, '/') + 1);
    char real[4096]; if (!realpath(link, real)) { printf("FAIL pci\n"); return 1; }
    snprintf(pci, sizeof pci, "%s", strrchr(real, '/') + 1); }
  CUdevice dev; CUcontext cctx; CUresult r;
  const char *name;
#define CHK(call, what) if ((r = (call))) { errname(r, &name); printf("FAIL %s %s (%d)\n", what, name, r); return 1; }
  CHK(init(0), "cuInit") CHK(bypci(&dev, pci), "cuDeviceGetByPCIBusId") CHK(retain(&cctx, dev), "retain") CHK(push(cctx), "push")
  CUdeviceptr out; size_t row = (size_t) w * 4;
  CHK(alloc(&out, row * h), "cuMemAlloc")

  // draw route: owned texture in the DRM format's memory order, registered with CUDA once.
  GLuint owned = 0, ofbo = 0, prog = 0, vao = 0;
  CUgraphicsResource owned_res = NULL;
  if (!strcmp(route, "draw")) {
    glGenTextures(1, &owned); glBindTexture(GL_TEXTURE_2D, owned);
    // R32UI: CUDA-GL takes integer formats but not RGB10_A2; the shader packs the DRM word itself.
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, w, h);
    glGenFramebuffers(1, &ofbo); glBindFramebuffer(GL_FRAMEBUFFER, ofbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, owned, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("FAIL owned fbo\n"); return 1; }
    // texelFetch of a UNORM texel is exactly k/(2^n-1), so round(v*(2^n-1)) recovers k bit-exactly.
    const char *vs = "#version 330 core\nout vec2 uv; void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2);"
                     " uv = p; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }\n";
    const char *fs8 = "#version 330 core\nuniform sampler2D s; out uint c; void main(){ uvec4 v = uvec4(round(texelFetch(s, ivec2(gl_FragCoord.xy), 0) * 255.0));"
                      " c = v.b | (v.g << 8) | (v.r << 16) | (255u << 24); }\n";
    const char *fs10 = "#version 330 core\nuniform sampler2D s; out uint c; void main(){ uvec3 v = uvec3(round(texelFetch(s, ivec2(gl_FragCoord.xy), 0).rgb * 1023.0));"
                      " c = v.r | (v.g << 10) | (v.b << 20) | (3u << 30); }\n";
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs)); glAttachShader(prog, shader(GL_FRAGMENT_SHADER, ten ? fs10 : fs8));
    glLinkProgram(prog); glUseProgram(prog); glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glViewport(0, 0, w, h);
    CHK(glreg(&owned_res, owned, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "cuGraphicsGLRegisterImage(owned)")
  }

  double best = 1e9, worst = 0, total = 0;
  const int loops = 60;
  for (int i = 0; i < loops; ++i) {
    double t0 = now_ms();
    CUgraphicsResource res = NULL;
    CUDA_MEMCPY2D c; memset(&c, 0, sizeof c);
    c.dstMemoryType = CU_MEMORYTYPE_DEVICE; c.dstDevice = out; c.dstPitch = row; c.WidthInBytes = row; c.Height = h;
    if (!strcmp(route, "egl") || !strcmp(route, "egltex")) {
      EGLImageKHR src = image;
      if (!strcmp(route, "egltex")) {
        src = create_image(dpy, ctx, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer) (uintptr_t) captex, NULL);
        if (!src) { printf("FAIL egltex image 0x%x\n", eglGetError()); return 1; }
      }
      CHK(reg(&res, src, flags), "cuGraphicsEGLRegisterImage")
      CUeglFrame frame;
      CHK(getframe(&frame, res, 0, 0), "cuGraphicsResourceGetMappedEglFrame")
      if (!i) printf("eglFrame type=%s %ux%u pitch=%u planes=%u colorFormat=%d cuFormat=0x%x channels=%u\n",
                     frame.frameType == CU_EGL_FRAME_TYPE_ARRAY ? "array" : "pitch", frame.width, frame.height,
                     frame.pitch, frame.planeCount, (int) frame.eglColorFormat, (unsigned) frame.cuFormat, frame.numChannels);
      if (frame.frameType == CU_EGL_FRAME_TYPE_ARRAY) { c.srcMemoryType = CU_MEMORYTYPE_ARRAY; c.srcArray = frame.frame.pArray[0]; }
      else { c.srcMemoryType = CU_MEMORYTYPE_DEVICE; c.srcDevice = (CUdeviceptr) frame.frame.pPitch[0]; c.srcPitch = frame.pitch; }
      CHK(copy2d(&c), "cuMemcpy2D") CHK(sync(), "sync") CHK(unreg(res), "unregister")
    } else if (!strcmp(route, "gl")) {
      CHK(glreg(&res, captex, GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY), "cuGraphicsGLRegisterImage(import)")
      CHK(map(1, &res, 0), "map")
      c.srcMemoryType = CU_MEMORYTYPE_ARRAY;
      CHK(getarray(&c.srcArray, res, 0, 0), "mapped array")
      CHK(copy2d(&c), "cuMemcpy2D") CHK(unmap(1, &res, 0), "unmap") CHK(sync(), "sync") CHK(unreg(res), "unregister")
    } else {
      glBindTexture(GL_TEXTURE_2D, captex);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      // map orders CUDA after the GL work issued so far (the interop contract), no glFinish needed.
      CHK(map(1, &owned_res, 0), "map")
      c.srcMemoryType = CU_MEMORYTYPE_ARRAY;
      CHK(getarray(&c.srcArray, owned_res, 0, 0), "mapped array")
      CHK(copy2d(&c), "cuMemcpy2D") CHK(unmap(1, &owned_res, 0), "unmap") CHK(sync(), "sync")
    }
    double t = now_ms() - t0; total += t; if (t < best) best = t; if (t > worst) worst = t;
  }
  printf("per-frame ms best=%.3f mean=%.3f worst=%.3f\n", best, total / loops, worst);
  unsigned char *host = malloc(row * h);
  CHK(dtoh(host, out, row * h), "dtoh")
  int pass = 1;
  for (int q = 0; q < 4; ++q) {
    // Memory row 0 is GL y=0, the bottom quadrants as painted (a compositor's frames start at the top).
    int x = (q & 1) ? 3 * w / 4 : w / 4, y = (q & 2) ? h / 4 : 3 * h / 4;
    uint32_t px; memcpy(&px, host + (size_t) y * row + (size_t) x * 4, 4);
    unsigned r8, g8, b8;
    if (ten) { r8 = (px & 1023) >> 2; g8 = ((px >> 10) & 1023) >> 2; b8 = ((px >> 20) & 1023) >> 2; }
    else { b8 = px & 255; g8 = (px >> 8) & 255; r8 = (px >> 16) & 255; }
    unsigned er = colours[q][0] * 255, eg = colours[q][1] * 255, eb = colours[q][2] * 255;
    int ok = r8 == er && g8 == eg && b8 == eb;
    pass &= ok;
    printf("quadrant %d px=0x%08x rgb=%u,%u,%u expect %u,%u,%u %s\n", q, px, r8, g8, b8, er, eg, eb, ok ? "ok" : "MISMATCH");
  }
  printf("RESULT %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 2;
}
