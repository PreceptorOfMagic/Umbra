/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct umbra_portal_color;
struct umbra_portal_hdr {
    int hdr; /* complete BT.2020/PQ target metadata, otherwise SDR */
    int32_t xy[8]; /* RGB + white, scaled 1 million */
    uint32_t min_luminance; /* 1/10000 nit */
    uint32_t max_luminance, max_cll, max_fall; /* nits */
};
/* Match exactly one output's logical position to the portal selection. Never
 * guess the primary monitor. Returns NULL on absent/ambiguous output metadata; SDR remains watched.
 * Calls are serialized; open has a total three-second protocol deadline. */
struct umbra_portal_color *umbra_portal_color_open(int x,int y,struct umbra_portal_hdr *metadata);
/* Nonblocking: 1 unchanged, 0 output/metadata changed or connection lost. */
int umbra_portal_color_current(struct umbra_portal_color *color);
void umbra_portal_color_close(struct umbra_portal_color *color);
#ifdef __cplusplus
}
#endif
