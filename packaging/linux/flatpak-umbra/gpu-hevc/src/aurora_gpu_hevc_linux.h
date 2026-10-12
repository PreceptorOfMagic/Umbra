// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "aurora_gpu_hevc.h"
#define AGHL_ABI_VERSION 3u
#define AGHL_API __attribute__((visibility("default")))
#ifdef __cplusplus
extern "C" {
#endif
typedef struct aghl_session aghl_session;
typedef struct aghl_config {
    uint32_t size,abi_version,width,height,bit_depth,hdr;
    const char* kernel_directory;
    uint32_t bitrate_kbps,framerate;
    const char* device_selector;
} aghl_config;
enum aghl_format { AGHL_BGRA8=1, AGHL_RGBA8=2, AGHL_SCRGB16F=3, AGHL_XBGR2101010_PQ=4 };
typedef struct aghl_frame {
    uint32_t size,width,height,format;
    size_t row_pitch,bytes;
    const void* data;
} aghl_frame;
// Serialized synchronous calls, no internal queue. Input is borrowed only for
// encode; the returned packet belongs to the session until the next call.
// Linux portal CPU buffers are uploaded to an OpenCL image; conversion, fitting
// and picture encoding execute on the GPU. This is not zero-copy capture.
// HDR accepts linear scRGB half-float (1.0 = 80 nits), or explicit packed
// little-endian xBGR2101010 BT.2020/ST2084 PQ (R in low10bits). PQ is HDR-only.
// Selection: empty = only GPU or single non-unified GPU; otherwise exact name
// or pci:dddd:bb:dd.f. Ambiguous/unavailable explicit choices fail, never fall back.
AGHL_API int aghl_probe(char* device,size_t device_capacity,char* error,size_t error_capacity);
AGHL_API int aghl_probe_device(const char* selector,char* device,size_t device_capacity,char* error,size_t error_capacity);
AGHL_API aghl_session* aghl_open(const aghl_config*,char* error,size_t error_capacity);
AGHL_API int aghl_encode(aghl_session*,const aghl_frame*,uint64_t frame_id,uint32_t force_idr,
                        agh_packet*,char* error,size_t error_capacity);
AGHL_API void aghl_close(aghl_session*);
#ifdef __cplusplus
}
#endif
