/* SPDX-License-Identifier: GPL-3.0-or-later
 * Ask a running KWin for a virtual output the way Umbra does (zkde_screencast_unstable_v1
 * stream_virtual_output), then use Umbra's own output control (src/platform/linux/kde-output.c)
 * as Umbra does for each stream: check KWin's output management is usable, resize the output to
 * the stream's mode, read whether KWin offers HDR on it and switch it on. Prints one JSON line, then
 * keeps the stream open until stdin closes so the caller can inspect the PipeWire node.
 *
 * With the HDR patch on KWin's DRM backend: hdr_capable 1, hdr_enabled 1, and the stream then
 * offers 10-bit formats. Stock KWin, or the headless virtual backend: hdr_capable 0.
 */
#include <wayland-client.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kde-output.h"
#include "zkde-screencast-unstable-v1.h"

static struct zkde_screencast_unstable_v1 *manager;
static int done, failed;
static uint32_t node = UINT32_MAX;
static char failure[256];

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
  if (!strcmp(interface, "zkde_screencast_unstable_v1") && version >= 2)
    manager = wl_registry_bind(registry, name, &zkde_screencast_unstable_v1_interface, 2);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void stream_closed(void *data, struct zkde_screencast_stream_unstable_v1 *s) { failed = 1; done = 1; }
static void stream_created(void *data, struct zkde_screencast_stream_unstable_v1 *s, uint32_t id) { node = id; done = 1; }
static void stream_failed(void *data, struct zkde_screencast_stream_unstable_v1 *s, const char *error) {
  snprintf(failure, sizeof failure, "%s", error);
  failed = 1;
  done = 1;
}
static const struct zkde_screencast_stream_unstable_v1_listener stream_listener = {stream_closed, stream_created, stream_failed};

static int roundtrip_until(struct wl_display *display, int *flag, int seconds) {
  for (int i = 0; i < seconds * 10 && !*flag; i++) {
    if (wl_display_dispatch_pending(display) < 0 || wl_display_flush(display) < 0) return 0;
    struct pollfd pfd = {wl_display_get_fd(display), POLLIN, 0};
    if (poll(&pfd, 1, 100) > 0 && wl_display_dispatch(display) < 0) return 0;
  }
  return *flag;
}

int main(void) {
  /* The name Umbra's output control accepts: Virtual-virtual-umbra-... */
  const char *name = "virtual-umbra-smoke";
  char output[128];
  snprintf(output, sizeof output, "Virtual-%s", name);

  struct wl_display *display = wl_display_connect(NULL);
  if (!display) { printf("{\"error\": \"no Wayland display\"}\n"); return 1; }
  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);
  if (!manager) { printf("{\"error\": \"KWin offers no zkde_screencast_unstable_v1 (screencast plugin not loaded)\"}\n"); return 1; }

  struct zkde_screencast_stream_unstable_v1 *stream =
    zkde_screencast_unstable_v1_stream_virtual_output(manager, name, 1280, 720, wl_fixed_from_int(1), 2);
  zkde_screencast_stream_unstable_v1_add_listener(stream, &stream_listener, NULL);
  if (!roundtrip_until(display, &done, 10) || failed) {
    printf("{\"error\": \"stream_virtual_output failed: %s\"}\n", failure[0] ? failure : "no answer");
    return 1;
  }

  int supported = umbra_kde_output_supported();
  int x = 0, y = 0;
  int resized = umbra_kde_output_resize(output, 1920, 1080, 60000, &x, &y);
  int capable = umbra_kde_output_hdr_capable(output);
  int enabled = capable ? umbra_kde_output_set_hdr(output, 1) : 0;
  wl_display_roundtrip(display);
  printf("{\"output\": \"%s\", \"node_id\": %u, \"output_control\": %d, \"resized\": %d, \"hdr_capable\": %d, \"hdr_enabled\": %d}\n",
         output, node, supported, resized, capable, enabled);
  fflush(stdout);

  /* Keep the stream (and its virtual output) until the caller is done with the node. */
  struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
  for (int i = 0; i < 300; i++) {
    wl_display_flush(display);
    if (poll(fds, 2, 100) > 0) {
      if (fds[1].revents) break;
      if (fds[0].revents && wl_display_dispatch(display) < 0) break;
    }
  }
  zkde_screencast_stream_unstable_v1_close(stream);
  wl_display_roundtrip(display);
  wl_display_disconnect(display);
  return 0;
}
