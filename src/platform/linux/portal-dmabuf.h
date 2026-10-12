// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "graphics.h"
#include <wayland-client.h>
#include <drm_fourcc.h>
#include <spa/param/video/raw.h>
#include <spa/buffer/buffer.h>
#include <vector>
#include <cstring>
#include <array>
#include <algorithm>
#include <limits>

namespace portal {
// Context and GL entry points belong to this capture instance. No global GL
// state is replaced while another encoder/capturer may be using it.
class dma_reader_t {
  wl_display *wayland = nullptr;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  GladGLContext gl {};
  PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
  PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
  using query_modifiers_t = EGLBoolean(EGLAPIENTRY *)(EGLDisplay,EGLint,EGLint,EGLuint64KHR*,EGLBoolean*,EGLint*);
public:
  std::vector<uint64_t> modifiers;
  ~dma_reader_t() {
    if (display != EGL_NO_DISPLAY) {
      if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
      eglTerminate(display);
    }
    if (wayland) wl_display_disconnect(wayland);
  }
  bool init() {
    if (!eglGetPlatformDisplay || !eglCreateContext || !eglMakeCurrent) return false;
    wayland = wl_display_connect(nullptr);
    if (!wayland) return false;
    display = eglGetPlatformDisplay(0x31D8 /* EGL_PLATFORM_WAYLAND_KHR */, wayland, nullptr);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_API)) return false;
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "EGL_EXT_image_dma_buf_import_modifiers") ||
        !std::strstr(extensions, "EGL_KHR_surfaceless_context")) return false;
    const EGLint attrs[] {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLConfig config; EGLint count = 0;
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || !count) return false;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return false;
    auto release = util::fail_guard([&] { eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
    if (!gladLoadGLContext(&gl, eglGetProcAddress) || !gl.EGLImageTargetTexture2DOES || !gl.GetTexImage) return false;
    create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    auto query = reinterpret_cast<query_modifiers_t>(eglGetProcAddress("eglQueryDmaBufModifiersEXT"));
    if (!create_image || !destroy_image || !query) return false;
    std::array<EGLuint64KHR,256> candidates {}; std::array<EGLBoolean,256> external {};
    if (!query(display, DRM_FORMAT_XBGR2101010, candidates.size(), candidates.data(), external.data(), &count) || count > int(candidates.size())) return false;
    for (int i=0;i<count;++i) if (!external[i]) modifiers.push_back(candidates[i]);
    BOOST_LOG(info) << "Portal HDR import GPU: " << gl.GetString(GL_RENDERER) << ", modifiers=" << modifiers.size();
    return !modifiers.empty();
  }
  // Borrowed DMA fds stay producer-owned. Readback/finish precedes requeue.
  bool read(const spa_buffer &buffer, const spa_video_info_raw &format, std::vector<uint8_t> &pixels) {
    if (!buffer.n_datas || buffer.n_datas > 4 || !(format.flags & SPA_VIDEO_FLAG_MODIFIER) ||
        format.format != SPA_VIDEO_FORMAT_xBGR_210LE ||
        std::find(modifiers.begin(), modifiers.end(), format.modifier) == modifiers.end()) return false;
    constexpr EGLint fd_attrs[] {0x3272,0x3275,0x3278,0x3440};
    constexpr EGLint offset_attrs[] {0x3273,0x3276,0x3279,0x3441};
    constexpr EGLint pitch_attrs[] {0x3274,0x3277,0x327A,0x3442};
    constexpr EGLint lo_attrs[] {0x3443,0x3445,0x3447,0x3449};
    constexpr EGLint hi_attrs[] {0x3444,0x3446,0x3448,0x344A};
    std::vector<EGLint> attrs {EGL_WIDTH,EGLint(format.size.width),EGL_HEIGHT,EGLint(format.size.height),
      0x3271 /* EGL_LINUX_DRM_FOURCC_EXT */,DRM_FORMAT_XBGR2101010};
    for (unsigned i=0;i<buffer.n_datas;++i) {
      const auto &d=buffer.datas[i];
      if (d.type != SPA_DATA_DmaBuf || d.fd < 0 || d.fd > std::numeric_limits<EGLint>::max() || !d.chunk ||
          d.chunk->stride <= 0 || d.chunk->offset > uint32_t(std::numeric_limits<EGLint>::max()) ||
          (d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)) return false;
      attrs.insert(attrs.end(), {fd_attrs[i],EGLint(d.fd),offset_attrs[i],EGLint(d.chunk->offset),pitch_attrs[i],d.chunk->stride,
        lo_attrs[i],EGLint(format.modifier & 0xffffffffu),hi_attrs[i],EGLint(format.modifier >> 32)});
    }
    attrs.push_back(EGL_NONE);
    if (!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context)) return false;
    auto release = util::fail_guard([&] { eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT); });
    auto image = create_image(display,EGL_NO_CONTEXT,0x3270 /* EGL_LINUX_DMA_BUF_EXT */,nullptr,attrs.data());
    if (!image) return false;
    auto release_image = util::fail_guard([&] { destroy_image(display,image); });
    GLuint texture = 0; gl.GenTextures(1,&texture);
    auto release_texture = util::fail_guard([&] { gl.DeleteTextures(1,&texture); });
    gl.BindTexture(GL_TEXTURE_2D,texture);
    gl.EGLImageTargetTexture2DOES(GL_TEXTURE_2D,image);
    if (gl.GetError() != GL_NO_ERROR) return false;
    pixels.resize(size_t(format.size.width)*format.size.height*4);
    gl.GetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_INT_2_10_10_10_REV,pixels.data());
    gl.Finish();
    return gl.GetError() == GL_NO_ERROR;
  }
};
}
