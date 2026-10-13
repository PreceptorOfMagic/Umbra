// Leg test for the AMD (Mesa rusticl) GPU-frame hand-off: rusticl has cl_khr_gl_sharing but no
// external memory, so the encoder's OpenCL context shares the GL stage's EGL context and reads the
// packed R32UI texture directly. Sequence exercised exactly as the library does it: CL context with
// CL_GL_CONTEXT_KHR + CL_EGL_DISPLAY_KHR, clCreateFromGLTexture, glFinish, acquire, copy image ->
// buffer, release, read back, compare with the words written into the texture.
// Runs on any Mesa EGL + rusticl pair (llvmpipe without an AMD card). Build in the Flatpak SDK:
//   gcc -O2 -I<OpenCL-Headers> glshare-probe.c -o glshare-probe -lEGL -lOpenGL -ldl
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD(name) name##_fn name = (name##_fn) dlsym(cl, #name); if (!name) { printf("FAIL dlsym %s\n", #name); return 1; }
typedef cl_int (*clGetPlatformIDs_fn)(cl_uint, cl_platform_id *, cl_uint *);
typedef cl_int (*clGetPlatformInfo_fn)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);
typedef cl_int (*clGetDeviceIDs_fn)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
typedef cl_int (*clGetDeviceInfo_fn)(cl_device_id, cl_device_info, size_t, void *, size_t *);
typedef cl_context (*clCreateContext_fn)(const cl_context_properties *, cl_uint, const cl_device_id *, void *, void *, cl_int *);
typedef cl_command_queue (*clCreateCommandQueue_fn)(cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
typedef cl_mem (*clCreateFromGLTexture_fn)(cl_context, cl_mem_flags, cl_GLenum, cl_GLint, cl_GLuint, cl_int *);
typedef cl_mem (*clCreateBuffer_fn)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
typedef cl_int (*clEnqueueAcquireGLObjects_fn)(cl_command_queue, cl_uint, const cl_mem *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*clEnqueueReleaseGLObjects_fn)(cl_command_queue, cl_uint, const cl_mem *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*clEnqueueCopyImageToBuffer_fn)(cl_command_queue, cl_mem, cl_mem, const size_t *, const size_t *, size_t, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*clEnqueueReadBuffer_fn)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *, cl_uint, const cl_event *, cl_event *);
typedef cl_int (*clGetImageInfo_fn)(cl_mem, cl_image_info, size_t, void *, size_t *);
typedef cl_int (*clFinish_fn)(cl_command_queue);

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const int w = argc > 1 ? atoi(argv[1]) : 1920, h = argc > 2 ? atoi(argv[2]) : 2176;
  PFNEGLGETPLATFORMDISPLAYEXTPROC getdisp = (void *) eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = getdisp(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize 0x%x\n", eglGetError()); return 1; }
  eglBindAPI(EGL_OPENGL_API);
  const EGLint ctx_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attrs);
  if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("FAIL context 0x%x\n", eglGetError()); return 1; }
  printf("EGL vendor=%s GL renderer=%s\n", eglQueryString(dpy, EGL_VENDOR), (const char *) glGetString(GL_RENDERER));

  // The packed texture, as the GL stage leaves it: one 32-bit word per pixel.
  GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, w, h);
  uint32_t *words = malloc((size_t) w * h * 4);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) words[(size_t) y * w + x] = (uint32_t) (x * 2654435761u ^ y * 40503u);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, words);
  glFinish();  // cl_khr_gl_sharing: GL work on the object completes before the acquire
  if (glGetError()) { printf("FAIL texture\n"); return 1; }
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);  // as the stage releases it

  void *cl = dlopen("libOpenCL.so.1", RTLD_NOW);
  if (!cl) { printf("FAIL libOpenCL\n"); return 1; }
  LOAD(clGetPlatformIDs) LOAD(clGetPlatformInfo) LOAD(clGetDeviceIDs) LOAD(clGetDeviceInfo) LOAD(clCreateContext)
  LOAD(clCreateCommandQueue) LOAD(clCreateFromGLTexture) LOAD(clCreateBuffer) LOAD(clEnqueueAcquireGLObjects)
  LOAD(clEnqueueReleaseGLObjects) LOAD(clEnqueueCopyImageToBuffer) LOAD(clEnqueueReadBuffer) LOAD(clGetImageInfo) LOAD(clFinish)
  cl_platform_id platforms[8]; cl_uint np = 0; clGetPlatformIDs(8, platforms, &np);
  int tested = 0, passed = 0;
  for (cl_uint p = 0; p < np; ++p) {
    char pname[256] = {0}, ext[8192] = {0};
    clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, sizeof pname, pname, NULL);
    cl_device_id dev; cl_uint nd = 0;
    if (clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 1, &dev, &nd) || !nd) continue;
    char dname[256] = {0};
    clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof dname, dname, NULL);
    clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, sizeof ext, ext, NULL);
    printf("platform %s device %s gl_sharing=%d\n", pname, dname, strstr(ext, "cl_khr_gl_sharing") != NULL);
    if (!strstr(ext, "cl_khr_gl_sharing")) continue;
    ++tested;
    const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties) platforms[p],
      CL_GL_CONTEXT_KHR, (cl_context_properties) ctx, CL_EGL_DISPLAY_KHR, (cl_context_properties) dpy, 0};
    cl_int e = 0;
    cl_context context = clCreateContext(props, 1, &dev, NULL, NULL, &e);
    if (e) { printf("  GL-sharing context: error %d\n", e); continue; }
    cl_command_queue queue = clCreateCommandQueue(context, dev, 0, &e);
    cl_mem image = clCreateFromGLTexture(context, CL_MEM_READ_ONLY, GL_TEXTURE_2D, 0, tex, &e);
    if (e) { printf("  clCreateFromGLTexture: error %d\n", e); continue; }
    cl_image_format format; clGetImageInfo(image, CL_IMAGE_FORMAT, sizeof format, &format, NULL);
    printf("  image format order=0x%x type=0x%x (expect CL_R 0x10B0 / CL_UNSIGNED_INT32 0x10DC)\n",
           format.image_channel_order, format.image_channel_data_type);
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE, (size_t) w * h * 4, NULL, &e);
    const size_t origin[3] = {0, 0, 0}, region[3] = {(size_t) w, (size_t) h, 1};
    if ((e = clEnqueueAcquireGLObjects(queue, 1, &image, 0, NULL, NULL))) { printf("  acquire: error %d\n", e); continue; }
    if ((e = clEnqueueCopyImageToBuffer(queue, image, buffer, origin, region, 0, 0, NULL, NULL))) { printf("  copy: error %d\n", e); continue; }
    if ((e = clEnqueueReleaseGLObjects(queue, 1, &image, 0, NULL, NULL))) { printf("  release: error %d\n", e); continue; }
    uint32_t *back = calloc((size_t) w * h, 4);
    if ((e = clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, (size_t) w * h * 4, back, 0, NULL, NULL))) { printf("  read: error %d\n", e); continue; }
    size_t bad = 0;
    for (size_t i = 0; i < (size_t) w * h; ++i) bad += back[i] != words[i];
    printf("  words compared=%zu mismatched=%zu %s\n", (size_t) w * h, bad, bad ? "MISMATCH" : "ok");
    passed += !bad;
  }
  printf("RESULT tested=%d passed=%d %s\n", tested, passed, tested && passed == tested ? "PASS" : "FAIL");
  return tested && passed == tested ? 0 : 2;
}
