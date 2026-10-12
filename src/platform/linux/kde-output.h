/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int umbra_kde_output_supported(void);
int umbra_kde_output_resize(const char *name,int width,int height,int milli_hz,int *x,int *y);
#ifdef __cplusplus
}
#endif
