// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "aurora_gpu_hevc.h"
#include <stddef.h>
#define AGHL_ABI_VERSION 3u
#define AGHL_API __attribute__((visibility("default")))
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aghl_session aghl_session;
// width/height are even visible dimensions; coded padding is cropped by SPS.
typedef struct aghl_config {
    uint32_t size,abi_version,width,height,bit_depth,hdr;
    const char* kernel_directory;
    uint32_t bitrate_kbps,framerate;
    const char* device_selector;
    // Since GL-shared frames; callers passing AGHL_CONFIG_BASE_SIZE omit them. The EGL display and
    // context the caller's GL stage renders into: when both are set and the selected GPU's OpenCL
    // shares that context (cl_khr_gl_sharing), AGHL_FRAME_GL_TEXTURE frames are accepted
    // (aghl_gl_sharing reports it). Otherwise the session is unchanged.
    void* egl_display;
    void* egl_context;
} aghl_config;
#define AGHL_CONFIG_BASE_SIZE (offsetof(aghl_config,egl_display))
enum aghl_format { AGHL_BGRA8=1, AGHL_RGBA8=2, AGHL_SCRGB16F=3, AGHL_XBGR2101010_PQ=4 };
// AGHL_FRAME_EXTERNAL: the pixels are not in data but in an exportable GPU allocation (an opaque
// POSIX fd, e.g. a CUDA VMM export) holding `bytes` with tightly packed rows. The library imports it
// once (cl_khr_external_memory_opaque_fd) and copies it to its input image on the GPU.
#define AGHL_FRAME_EXTERNAL 1u
// AGHL_FRAME_GL_TEXTURE: the pixels are in gl_texture, a GL_TEXTURE_2D GL_R32UI texture of the
// session's shared EGL context holding one packed word per pixel (the format's byte layout). The
// caller's GL work on it is complete (glFinish) before aghl_encode; the library acquires it, copies
// it on the GPU and releases it. Mesa rusticl offers cl_khr_gl_sharing but no external memory.
#define AGHL_FRAME_GL_TEXTURE 2u
// aghl_encode result when an external frame cannot be imported by this runtime: nothing was
// encoded and the session is intact; resubmit the same frame id with host pixels.
#define AGHL_EXTERNAL_UNAVAILABLE (-2)
typedef struct aghl_frame {
    uint32_t size,width,height,format;
    size_t row_pitch,bytes;
    const void* data;
    uint32_t flags;      // since the external-frame extension; older callers pass a smaller size
    int32_t external_fd;
    uint32_t gl_texture;
    // Identifies the external allocation or texture: it changes whenever that object is replaced,
    // so a recycled fd number or texture name is never matched to a stale import.
    uint64_t external_serial;
} aghl_frame;
#define AGHL_FRAME_BASE_SIZE (offsetof(aghl_frame,flags))
// Serialized synchronous calls, no internal queue. Input is borrowed only for
// encode; the returned packet belongs to the session until the next call.
// Linux portal CPU buffers are uploaded to an OpenCL image; conversion, fitting
// and picture encoding execute on the GPU. External frames skip that upload.
// HDR accepts linear scRGB half-float (1.0 = 80 nits), or explicit packed
// little-endian xBGR2101010 BT.2020/ST2084 PQ (R in low10bits). PQ is HDR-only.
// Selection: empty = only GPU or single non-unified GPU; otherwise exact name
// or pci:dddd:bb:dd.f. Ambiguous/unavailable explicit choices fail, never fall back.
AGHL_API int aghl_probe(char* device,size_t device_capacity,char* error,size_t error_capacity);
AGHL_API int aghl_probe_device(const char* selector,char* device,size_t device_capacity,char* error,size_t error_capacity);
AGHL_API aghl_session* aghl_open(const aghl_config*,char* error,size_t error_capacity);
AGHL_API int aghl_encode(aghl_session*,const aghl_frame*,uint64_t frame_id,uint32_t force_idr,
                        agh_packet*,char* error,size_t error_capacity);
// 1 when the session's OpenCL context shares aghl_config's EGL context (GL texture frames accepted).
AGHL_API int aghl_gl_sharing(const aghl_session*);
AGHL_API void aghl_close(aghl_session*);
#ifdef __cplusplus
}
#endif
