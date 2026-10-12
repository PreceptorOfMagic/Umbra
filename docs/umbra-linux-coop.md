# Linux Co-op Pane: Flatpak capture feasibility

Status: **capture prototype only, not an implemented or released Linux Co-op Pane** (2026-10-12).
The chosen product is one Apollo-based Umbra Flatpak for broad Linux distribution support. A separate
Vibepollo Linux edition is deferred. Borrowing compatible GPL capture code remains an option.

## What is established

The ordinary XDG ScreenCast portal can create a virtual monitor for the sandboxed Umbra app. On
Ubuntu 26.04.1, Plasma 6.6.6 and NVIDIA RTX 2060 SUPER / 595.99.02, the direct PipeWire diagnostic
in `tests/manual/portal-pipewire-probe.c` received actual desktop pixels inside the installed Flatpak
with the existing freedesktop 23.08 runtime. No host escape or root capture was used.

The portal created a 1920x1080 virtual output after its normal desktop consent dialog. Changing the
output to 1920x2160 at 165 Hz using **host-side** KScreen caused the direct PipeWire client to
renegotiate from BGRx 1920x1080 / maxFramerate 60 to BGRx 1920x2160 / maxFramerate 165. Captured
images showed the desktop and the running glxgears window. Closing the session removed the virtual
output; the physical output was restored and all owned diagnostic processes were verified stopped.

This is not a 165 fps capture result. The MemFd/CPU-copy probe delivered about 61–63 buffers/sec in
three five-second motion windows (304, 314 and 313 buffers). Source rendering slowed to roughly
102–137 fps while it ran. The bottleneck has not been isolated. No encoded Eclipse/TV stream was
run during this probe, so it proves neither streaming quality nor co-op correctness or latency.

## Rejected diagnostic paths

GStreamer's default minimum of eight PipeWire buffers conflicts with KWin's offered range of two
to four on this machine. Requesting two to four fixed the initial allocation error. It did not fix
resizing: the 23.08 runtime's PipeWire 0.3.83 GStreamer plugin stalled on the old size; reconnecting
produced correctly sized but black images. A compositor screenshot simultaneously showed the
normal desktop and gears. The 25.08 runtime's PipeWire 1.4.9 plugin instead reported an unhandled
format on resize. Its earlier images were stale 1080p images. Do not use those paths as evidence
that the resized screen works, or infer a KWin restriction from their failures: the direct client
on runtime 23.08 captured the resized screen correctly.

## Protocol and implementation boundaries

- [XDG ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
  defines source type 4 as a virtual monitor and returns a PipeWire remote fd. First consent must
  remain the normal desktop prompt; persistent permission and token rotation still need testing.
- KWin's direct screencast interface is filtered for sandboxed clients in its
  [interface policy](https://invent.kde.org/plasma/kwin/-/blob/Plasma/6.6/src/wayland_server.cpp).
  Porting a native KWin capturer alone is insufficient for the Flatpak.
- KDE's portal source creates its virtual monitor at 1920x1080. The successful prototype used
  `kscreen-doctor` outside the sandbox to change that mode. **Self-contained, in-sandbox mode
  control is still unimplemented and unproven.** Do not ship a test-machine SSH command as setup.
- The prototype uses MemFd, not DMA-BUF or an encoder. Production capture should reuse an audited
  PipeWire backend, preserve buffer ownership, handle format changes, and verify hardware encoding.
- Linux currently has no built-in Co-op Pane. `proc::coop_pane_supported()` must remain false there
  until a usable implementation exists. Co-op and GPU-HEVC capability bits cannot advertise a plan.

## Manual diagnostic

Build the standalone C diagnostic against GIO and PipeWire, preferably with the same Flatpak SDK
as the app. It is not included in the host binary:

```sh
cc -O2 -Wall -Wextra -Wno-unused-parameter \
  tests/manual/portal-pipewire-probe.c -o portal-pw-probe \
  $(pkg-config --cflags --libs gio-unix-2.0 libpipewire-0.3)
```

Run from a dedicated diagnostic directory, inside the installed app's sandbox, using an absolute
path visible there. `virtual 60` requests one virtual screen for 60 seconds after consent;
`monitor 60` requests an existing monitor. An optional fourth argument defers connecting capture
until SIGUSR1; subsequent SIGUSR1 reconnects it without closing the portal session. SIGINT,
SIGTERM or the duration limit closes the portal session. The consent wait times out after 120s.

The probe writes `portal-frame.ppm` at the first frame, each geometry change and every 300 frames,
overwriting that diagnostic file. It prints format, sequence/timestamps and five-second buffer
counts. Exit zero means only that at least one non-corrupt buffer arrived without a PipeWire error.
It is **not** a visual, motion, latency or sustained-rate verdict. Inspect fresh pixels and the
sequence/timestamps; compare the real compositor and captured image. Always stop owned renderers
and restore the original output configuration from an EXIT/INT/TERM/HUP trap.

## Next work

1. Prove mode control from inside Flatpak, without a distro-specific install step.
2. Port audited portal/PipeWire capture into the Apollo-based host and validate GPU buffer handling.
3. Integrate pane creation, requested geometry/rate, game placement, Disconnect/resume and Quit cleanup.
4. Advertise support only where the pane is available; retain existing X11/KMS/NvFBC fallbacks elsewhere.
5. Run live encoded Eclipse/TV tests with inspected screenshots, then rebuild and stage the release.
   GNOME and other desktops, persistence, 165 fps capture, HDR and Linux co-op remain unverified.
