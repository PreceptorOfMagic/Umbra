# Linux Co-op Pane: Flatpak capture feasibility

Status: **portal capture backend implemented and under validation; Linux Co-op Pane is not implemented or released** (2026-10-12).
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
  `kscreen-doctor` outside the sandbox to change that mode. A subsequent standalone diagnostic
  (`tests/manual/kde-mode-probe.c`) successfully selects and creates custom modes through KDE's
  output-management protocol **inside the existing Flatpak permissions**. It limits changes to the
  exact Umbra portal output, places it beside existing outputs, and checks `current_mode` after
  `applied`. Inspected captures show gears at3840x1080 and1920x2160; no physical output was disabled.
  This controller still needs a production implementation with bounded dispatch, ownership and
  topology checks. These KDE protocols are private/versioned, not a promise of support on GNOME.
- The prototype uses MemFd, not DMA-BUF or an encoder. Production capture should reuse an audited
  PipeWire backend, preserve buffer ownership, handle format changes, and verify hardware encoding.
  The new host backend uses the measured MemFd path with an owned copy per frame. It has not yet
  established DMA-BUF or hardware-encoder performance.
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
overwriting that diagnostic file. SIGUSR2 requests a save of the next valid frame. The first resize
frame can show a transitional or black image: explicitly request and inspect a later frame. It prints
format, sequence/timestamps and five-second buffer
counts. Exit zero means only that at least one non-corrupt buffer arrived without a PipeWire error.
It is **not** a visual, motion, latency or sustained-rate verdict. Inspect fresh pixels and the
sequence/timestamps; compare the real compositor and captured image. Always stop owned renderers
and restore the original output configuration from an EXIT/INT/TERM/HUP trap.

## Host capture implementation

`SUNSHINE_ENABLE_PORTAL` enables `src/platform/linux/portalgrab.cpp`. It defaults on for Flatpak;
other packages opt in and need GIO>=2.66 and PipeWire>=0.3.40 development dependencies. The web UI
now offers Desktop portal; `capture=portal` selects it. Automatic capture prioritizes the portal
inside a Wayland Flatpak, where enumerating DRM outputs does not prove capture permission. Native
X11/NvFBC/KMS selection is preserved; native Wayland can use the portal as a fallback.

The backend requests one screen through the normal desktop picker, handles cancellation/timeouts,
and stores rotating restore tokens with mode0600, scoped by desktop when its environment identifier
is available. It retains the granted portal session through encoder probes, copies buffers before
returning them to PipeWire, checks bounds/corruption, timestamps arrivals on the host steady clock,
and asks the encoder to reinitialize on a geometry change. Shutdown interrupts a pending consent
wait and closes the session. The CPU image path connects to existing software/VAAPI/CUDA upload
interfaces, but hardware encoding is not established by capture tests.

`tests/manual/portal-capture-probe.cpp` exercises the actual host capture class inside the installed
Flatpak, with a test-only logging/config/appdata adapter and no encoder. Measured on VEGA: monitor
capture returned real desktop/gears pixels; subsequent launches restored permission without input.
A monitor run with the desktop asleep produced BLACK pixels despite valid buffers: it is void as
motion evidence. Waking the display before the repeat produced visibly correct gears. The host now
requests XDG Inhibit(suspend|idle) only while streams are active, releases it on the last stream's
stop and shutdown, and does not block logout/user switching. KDE's ActiveInhibitions reports
Umbra's sleep:idle request. A100-second run kept real gears visible at the end (inspected), then
ActiveInhibitions returned empty and all owned capture/render processes stopped.

The standalone mode diagnostic requires the two pinned KDE XML files under `tests/manual/protocols`.
Generate each with `wayland-scanner client-header` and `wayland-scanner private-code`; compile its
C source plus the generated C files against wayland-client. With no arguments it enumerates only;
`1920 2160 165000` requests width, height, milliHz on the exact Umbra virtual output. It refuses
physical outputs, unsupported protocol versions and ambiguous matching outputs. It is not installed
as an end-user helper. The XML is MIT-CMU, retained unchanged from plasma-wayland-protocols v1.20.0.

## Release requirements still open

1. Integrate virtual-pane creation, its in-sandbox mode controller, placement, Disconnect/resume
   and Quit cleanup with the actual host session lifecycle. Co-op caps stay clear until this works.
2. Validate hardware encoding and optimize capture throughput, then run live Eclipse/TV tests with
   inspected screenshots. The observed virtual capture is around60fps, not a proven165fps path.
3. Replace freedesktop23.08 for release: Flathub metadata on2026-10-12 marks it end-of-life. CUDA12.6's
   compiler constraint is a build-toolchain problem, not justification to ship an unsupported runtime.
   Select and test a supported toolchain without silently dropping older NVIDIA GPUs.
4. Test clean installation, input-device setup, permission revocation, restart and uninstall on KDE
   and GNOME, then a wlroots desktop. Flatpak portability does not imply identical compositor features.
   Make missing features explicit in setup while preserving ordinary desktop streaming where usable.
5. Produce a staged, reproducible Flatpak and compatibility matrix before publication. GNOME virtual
   panes, HDR, DMA-BUF, high-rate capture and Linux co-op remain unverified.
