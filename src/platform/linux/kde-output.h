/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int umbra_kde_output_supported(void);
int umbra_kde_output_resize(const char *name,int width,int height,int milli_hz,int *x,int *y);
/* Windows' exclusive virtual-display layout: disable every other enabled output, recording
 * their names in state_path BEFORE anything changes, and move the Umbra output to 0,0. */
int umbra_kde_output_exclusive(const char *name,const char *state_path);
/* Re-enable the outputs recorded in state_path; removes the file once none is left disabled.
 * Returns 1 when nothing remains to restore (including no file). */
int umbra_kde_output_restore(const char *state_path);
/* The Umbra output can be switched to HDR (KWin with virtual-output HDR). */
int umbra_kde_output_hdr_capable(const char *name);
/* Switch the Umbra output's HDR on (1) or off (0); 1 when KWin reports that state. */
int umbra_kde_output_set_hdr(const char *name,int enable);
#ifdef __cplusplus
}
#endif
