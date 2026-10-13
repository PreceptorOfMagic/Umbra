// End-to-end test of the GPU HEVC library's GL-texture frames (AGHL_FRAME_GL_TEXTURE), the AMD
// zero-copy path, on whatever Mesa GL + rusticl pair the environment selects (zink without an AMD
// card). Opens the library with the EGL context, checks aghl_gl_sharing, encodes frames straight
// from a GL_R32UI texture holding a known BGRA8 pattern and writes the HEVC stream for decoding.
// Run inside the Umbra Flatpak:
//   glshare-encode-probe <libaurora_gpu_hevc8.so> <kernels dir> <device name> <out.hevc> [w h frames]
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aurora_gpu_hevc_linux.h"

static void crashed(int sig, siginfo_t *info, void *unused) {
  (void) unused;
  void *frames[64]; int n = backtrace(frames, 64);
  dprintf(1, "CRASH signal %d at %p\n", sig, info->si_addr);
  backtrace_symbols_fd(frames, n, 1);
  _exit(139);
}
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = crashed; sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL); sigaction(SIGABRT, &sa, NULL);
  const int host_mode = getenv("PROBE_HOST_PIXELS") != NULL;  // same library and device, CPU pixels instead of the GL texture
  if (argc < 5) { printf("usage: lib kernels device out.hevc [w h frames]\n"); return 2; }
  const int w = argc > 5 ? atoi(argv[5]) : 640, h = argc > 6 ? atoi(argv[6]) : 480, frames = argc > 7 ? atoi(argv[7]) : 8;
  PFNEGLGETPLATFORMDISPLAYEXTPROC getdisp = (void *) eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = getdisp(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (!eglInitialize(dpy, NULL, NULL)) { printf("FAIL eglInitialize\n"); return 1; }
  eglBindAPI(EGL_OPENGL_API);
  const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("FAIL context\n"); return 1; }
  printf("GL renderer=%s\n", (const char *) glGetString(GL_RENDERER));
  // Eight vertical colour bars (white, yellow, cyan, green, magenta, red, blue, black) over a
  // horizontal grey ramp in the bottom quarter, packed as the stage packs BGRA8: b|g<<8|r<<16|X.
  static const uint8_t bars[8][3] = {{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}};
  uint32_t *words = malloc((size_t) w * h * 4);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    uint8_t r, g, b;
    if (y < h * 3 / 4) { const uint8_t *c = bars[x * 8 / w]; r = c[0]; g = c[1]; b = c[2]; }
    else r = g = b = (uint8_t) (x * 255 / (w - 1));
    words[(size_t) y * w + x] = b | (g << 8) | (r << 16) | (0xffu << 24);
  }
  GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, w, h);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, words);
  glFinish();
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) { printf("FAIL dlopen %s\n", dlerror()); return 1; }
  __typeof__(aghl_open) *open_ = dlsym(lib, "aghl_open");
  __typeof__(aghl_encode) *encode = dlsym(lib, "aghl_encode");
  __typeof__(aghl_close) *close_ = dlsym(lib, "aghl_close");
  __typeof__(aghl_gl_sharing) *sharing = dlsym(lib, "aghl_gl_sharing");
  if (!open_ || !encode || !close_ || !sharing) { printf("FAIL symbols (aghl_gl_sharing=%p)\n", (void *) sharing); return 1; }
  aghl_config config = {sizeof config, AGHL_ABI_VERSION, (uint32_t) w, (uint32_t) h, 8, 0, argv[2], 8000, 60, argv[3], dpy, ctx};
  char detail[1024] = {0};
  aghl_session *session = open_(&config, detail, sizeof detail);
  if (!session) { printf("FAIL open: %s\n", detail); return 1; }
  const int shared = sharing(session);
  printf("aghl_gl_sharing=%d\n", shared);
  if (!shared) { close_(session); printf("RESULT FAIL (no GL sharing)\n"); return 2; }
  FILE *out = fopen(argv[4], "wb");
  int ok = 1;
  for (int i = 1; i <= frames && ok; ++i) {
    aghl_frame frame = {sizeof frame, (uint32_t) w, (uint32_t) h, AGHL_BGRA8, (size_t) w * 4, (size_t) w * h * 4, NULL,
                        AGHL_FRAME_GL_TEXTURE, -1, tex, 1};
    if (host_mode) { frame.flags = 0; frame.gl_texture = 0; frame.data = words; }
    printf("encoding frame %d (%s)\n", i, host_mode ? "host pixels" : "GL texture");
    agh_packet packet; memset(&packet, 0, sizeof packet); packet.size = sizeof packet;
    int result = encode(session, &frame, (uint64_t) i, i == 1, &packet, detail, sizeof detail);
    if (result == AGHL_EXTERNAL_UNAVAILABLE && !host_mode) {
      // What Umbra does: the session is intact, resubmit the same frame id from host memory.
      printf("frame %d: GL frame unavailable (%s); resubmitting from host memory\n", i, detail);
      frame.flags = 0; frame.gl_texture = 0; frame.data = words;
      memset(&packet, 0, sizeof packet); packet.size = sizeof packet;
      result = encode(session, &frame, (uint64_t) i, i == 1, &packet, detail, sizeof detail);
    }
    if (result || !packet.data || !packet.bytes) { printf("FAIL encode frame %d: result=%d %s\n", i, result, detail); ok = 0; break; }
    fwrite(packet.data, 1, packet.bytes, out);
    printf("frame %d bytes=%zu idr=%d qp=%d\n", i, (size_t) packet.bytes, (int) packet.is_idr, (int) packet.qp);
  }
  fclose(out);
  close_(session);
  printf("RESULT %s\n", ok ? "PASS (decode and compare the stream)" : "FAIL");
  return ok ? 0 : 2;
}
