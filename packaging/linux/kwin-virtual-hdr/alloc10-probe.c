// What KWin's screencast does per buffer (DmaBufScreenCastBuffer::create), for each importable
// modifier of a format: GBM allocation with exactly that modifier, EGL import as GL_TEXTURE_2D,
// framebuffer completeness. Build: gcc -O2 alloc10-probe.c -o alloc10-probe -lEGL -lOpenGL -lgbm
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*target_fn)(GLenum, void *);
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
  int w = argc > 2 ? atoi(argv[2]) : 3840, h = argc > 3 ? atoi(argv[3]) : 2160;
  int drm = open(node, O_RDWR | O_CLOEXEC);
  struct gbm_device *gbm = gbm_create_device(drm);
  PFNEGLGETPLATFORMDISPLAYEXTPROC getdisp = (void *) eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = getdisp(EGL_PLATFORM_GBM_KHR, gbm, NULL);
  eglInitialize(dpy, NULL, NULL);
  eglBindAPI(EGL_OPENGL_API);
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
  PFNEGLQUERYDMABUFMODIFIERSEXTPROC query = (void *) eglGetProcAddress("eglQueryDmaBufModifiersEXT");
  PFNEGLCREATEIMAGEKHRPROC create_image = (void *) eglGetProcAddress("eglCreateImageKHR");
  target_fn target = (target_fn) eglGetProcAddress("glEGLImageTargetTexture2DOES");
  const uint32_t formats[] = {GBM_FORMAT_XBGR2101010, GBM_FORMAT_ABGR2101010, GBM_FORMAT_XRGB8888};
  for (int f = 0; f < 3; ++f) {
    uint32_t fmt = formats[f];
    EGLuint64KHR mods[64]; EGLBoolean ext[64]; EGLint n = 0;
    query(dpy, fmt, 64, mods, ext, &n);
    printf("== %.4s: %d modifiers\n", (char *) &fmt, n);
    for (int i = 0; i < n; ++i) {
      uint64_t mod = mods[i];
      struct gbm_bo *bo = gbm_bo_create_with_modifiers2(gbm, w, h, fmt, &mod, 1, GBM_BO_USE_RENDERING);
      if (!bo) { printf("  0x%016llx ext=%d ALLOC FAIL\n", (unsigned long long) mod, ext[i]); continue; }
      int planes = gbm_bo_get_plane_count(bo);
      int fd = gbm_bo_get_fd(bo);
      EGLint attrs[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, (EGLint) fmt,
        EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint) gbm_bo_get_offset(bo, 0),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint) gbm_bo_get_stride_for_plane(bo, 0),
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint) (gbm_bo_get_modifier(bo) & 0xffffffff),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint) (gbm_bo_get_modifier(bo) >> 32), EGL_NONE};
      EGLImageKHR image = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
      const char *result = "ok";
      if (!image) result = "IMPORT FAIL";
      else {
        GLuint tex, fbo; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        while (glGetError()) {}
        target(GL_TEXTURE_2D, image);
        GLenum err = glGetError();
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (err) result = "TEXTURE FAIL";
        else if (status != GL_FRAMEBUFFER_COMPLETE) result = "FBO INCOMPLETE";
        glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
      }
      printf("  0x%016llx ext=%d planes=%d stride=%u %s\n", (unsigned long long) mod, ext[i], planes,
             gbm_bo_get_stride_for_plane(bo, 0), result);
      gbm_bo_destroy(bo);
    }
  }
  return 0;
}
