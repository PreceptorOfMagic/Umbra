# Linux Co-op Pane: Flatpak capture feasibility

Status: **portal capture and KDE virtual-pane lifecycle implemented, under validation; not released** (2026-10-12).
The chosen product is one Apollo-based Umbra Flatpak for broad Linux distribution support. A separate
Vibepollo Linux edition is deferred. Borrowing compatible GPL capture code remains an option.

## Final release scope agreed 2026-10-12

Finish compatibility engineering and internal validation before handing the user a final
acceptance candidate. Intermediate builds are development artifacts. Broad installation alone
does not satisfy the release requirement.

The custom GPU HEVC encoder is required for Linux mixed-hardware co-op, with the same user-facing
behavior and encoding contract as the Windows version. It is not a deferred feature. Qualify
Linux/Linux and Linux/Windows sessions, both host roles and both pane orientations against the
corresponding supported Windows combinations. Preserve the shared coding geometry, rate control,
recovery behavior, game-resolution fitting, Disconnect/Resume and Quit behavior. Compare live
visual quality, latency and game-load performance with the identified Windows reference build;
compiling the OpenCL kernels is not parity evidence. Unsupported runtime/device combinations must
be explained without silently substituting a CPU encoder or an incompatible hardware bitstream.

HDR is also in scope wherever the complete path supports it, including the custom encoder's
10-bit path. Detect capture color representation and metadata, preserve the intended transfer
function and primaries through conversion and encoding, and verify client/display presentation.
Ten-bit encoding by itself is not HDR evidence. Qualify ordinary streaming and co-op separately;
co-op requires a compatible color contract across both panes. Where that is unavailable, select a
valid SDR path with a clear explanation, retaining the required mixed-hardware encoder. Do not
label an HDR buffer as SDR without appropriate capture negotiation or tone mapping. NVIDIA is a
target for investigation rather than a permanent blanket exclusion; actual support remains
dependent on the measured capture/driver/encoder path. Normal operation must stay unprivileged.

### Source audit and implementation order

1. Identify and pin the Windows encoder source/artifact used as the compatibility reference.
   The core in Eclipse's `experimental/gpu-hevc` uses OpenCL and includes a Linux runtime loader,
   but its production library currently requires D3D11 capture and its production CMake target
   requires MSVC. Port the platform boundary while retaining the shared encoder implementation.
2. Add Linux capture-to-OpenCL integration, source-built 8/10-bit libraries and Flatpak runtime
   discovery/packaging. Verify the actual GPU runtime inside the sandbox, not only on the host.
   Extend host capability discovery only when the implementation and required runtime are usable.
3. Establish SDR mixed-hardware compatibility with live encodes and inspected client/TV images,
   including game geometry changes, reconnects and recovery. Retain the Windows behavior as the
   reference and avoid changing its ABI merely to introduce a Linux adapter.
4. Extend capture and conversion for HDR. The current portal backend negotiates only BGRx/BGRA,
   and does not implement HDR detection/metadata; the existing CUDA CPU-upload path rejects P010.
   These require implementation, not a capability-bit change. Audit compositor-specific support
   before selecting the HDR capture route; then validate true HDR and SDR fallback end to end.
5. Include the encoder libraries, kernels, provenance, licenses and supported-runtime checks in
   the final reproducible package. Publish the measured desktop/GPU/feature matrix with the
   acceptance candidate. Source review and build results must remain distinct from runtime proof.

All of these are open release gates. The evidence sections below describe earlier development
builds and must not be read as completion of this expanded scope.

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
  defines source type 4 as a virtual monitor and returns a PipeWire remote fd. First consent remains
  the normal desktop prompt. Monitor and virtual permissions have both restored without further input
  in repeated processes on the tested KDE desktop; revocation still needs testing.
- KWin's direct screencast interface is filtered for sandboxed clients in its
  [interface policy](https://invent.kde.org/plasma/kwin/-/blob/Plasma/6.6/src/wayland_server.cpp).
  Porting a native KWin capturer alone is insufficient for the Flatpak.
- KDE's portal source creates its virtual monitor at 1920x1080. The successful prototype used
  `kscreen-doctor` outside the sandbox to change that mode. A subsequent standalone diagnostic
  (`tests/manual/kde-mode-probe.c`) successfully selects and creates custom modes through KDE's
  output-management protocol **inside the existing Flatpak permissions**. It limits changes to the
  exact Umbra portal output, places it beside existing outputs, and checks `current_mode` after
  `applied`. Inspected captures show gears at 3840x1080 and 1920x2160; no physical output was disabled.
  The host now includes a bounded controller restricted to that exact virtual output;
  it checks protocol versions, current mode and applied placement. These KDE protocols are private/versioned, not a promise of support on GNOME.
- The prototype uses MemFd, not DMA-BUF or an encoder. Production capture should reuse an audited
  PipeWire backend, preserve buffer ownership, handle format changes, and verify hardware encoding.
  The new host backend uses the measured MemFd path with an owned copy per frame. It has not yet
  established DMA-BUF or hardware-encoder performance.
- The private development host exposes its built-in Co-op Pane only inside Umbra Flatpak when the
  portal offers virtual sources and the required KDE output protocols are present. Other Linux
  desktops leave that capability clear. Linux GPU-HEVC remains unsupported.

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
Umbra's sleep:idle request. A 100-second run kept real gears visible at the end (inspected), then
ActiveInhibitions returned empty and all owned capture/render processes stopped.

The standalone mode diagnostic requires the two pinned KDE XML files under `tests/manual/protocols`.
Generate each with `wayland-scanner client-header` and `wayland-scanner private-code`; compile its
C source plus the generated C files against wayland-client. With no arguments it enumerates only;
`1920 2160 165000` requests width, height, milliHz on the exact Umbra virtual output. It refuses
physical outputs, unsupported protocol versions and ambiguous matching outputs. It is not installed
as an end-user helper. The XML is MIT-CMU, retained unchanged from plasma-wayland-protocols v1.20.0.

## Virtual-pane lifecycle validation

The actual host capture/controller classes, built with SDK 23.08 and run in the installed Umbra
sandbox, created 1920x2160@165, captured real gears, destroyed capture while retaining permission,
changed to 3840x1080@120 and captured real gears again. Both saved images were inspected. The second
process restored virtual-screen permission without input. Releasing the session removed the virtual
output; DP-1 remained enabled at 1920x1080@60. All owned gears/capture processes were stopped.

That sequence caught a reconnect bug: duplicating the first PipeWire socket worked only once.
Each new capture core now calls OpenPipeWireRemote for a new connection. The same lifecycle test
then passed with 744 tall and 974 wide buffers in 18-second phases. These are capture checks, not
an encoder benchmark, end-to-end frame-rate claim or TV verdict. The full host test below followed.

Host launch and co-op resume request the client's geometry; Quit releases the virtual session.
Normal discovery only reads capabilities and never opens a consent picker. The mode controller
changes only the exact portal-created Umbra output, preserving physical outputs. Game placement,
full session behavior and fractional-scale pointer routing remain to be verified.

## Full host / client check (2026-10-12)

The full CUDA-enabled host built with the existing SDK 23.08 dependencies and ran as an ordinary
user inside the installed Umbra Flatpak sandbox on VEGA. H.264 and HEVC NVENC probes succeeded.
Eclipse 1.3.4 on an isolated Xvfb client received live HEVC/SDR desktop video. Inspected captures
showed gears; a streamed mouse drag moved their window. Disconnect and Resume restored video.

A temporary separate apps.json added a virtual-screen test app; the installed apps.json was kept.
Launching it created the portal output beside DP-1 and streamed its live contents. A screenshot
at 61 seconds showed gears. Disconnect retained the output, Resume restored HEVC video without
new consent, and a deliberate host-side window movement was visible on the client. Quit removed
only the virtual output. Active idle inhibitors were empty after teardown. No owned renderer,
host, client or Xvfb remained. This tests the virtual-display host path; it is NOT a two-host
co-op splice, geometry-changing co-op resume or TV verdict. Virtual-screen pointer routing is
still unverified (the ordinary desktop mouse test does not cover that offset).

The TV was reachable by developer SSH, but the remote-control websocket rejected the connection
(code1008). No TV stream or TV quality verdict was claimed. The inherited native X11 startup
heap abort also reproduced with the pre-portal baseline binary; its cause remains OPEN. The
XInitThreads experiment did not fix it and was reverted. CUDA's existing CPU upload path rejected
10-bit/P010, and /dev/uhid permissions disabled DualSense emulation on this host. These remain
release limitations, not passing test results. The inherited pairing unit tests still fail to
compile against the existing Umbra API; the production target built with BUILD_TESTS=OFF.
Standalone manual diagnostics are excluded from the unit-test source glob.

## Release requirements still open

1. Validate the integrated virtual pane through actual host launch, Disconnect/resume and Quit,
   plus game placement. The development build is not yet a release candidate.
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

## Runtime25.08 and input setup (2026-10-12)

The rebuilt Flatpak uses Freedesktop25.08, CUDA12.9 and source-built GCC14.4 for nvcc.
The toolkit needs six math declaration exception specifications matched to glibc2.42; CUDA objects
also need explicit position-independent code for the SDK's PIE link. Compilation includes the
existing Maxwell-through-Hopper architecture list. This is not old-GPU execution validation.
The exported application excludes the compiler/toolkit and their debug/source payloads.

Installed on VEGA as a separate portal-test branch, the runtime25 package passed live NVENC HEVC
SDR desktop and virtual-display streaming to Eclipse1.3.4, including Disconnect/Resume/Quit.
Desktop images after more than a minute and virtual images after more than two minutes were
inspected. A host-side window movement after resume reached the client. Quit removed the virtual
output while preserving DP-1. This remains a single-host test, not a two-host splice or TV result.

A live absolute-pointer drag on the non-origin virtual display initially failed. Source traced
this to inputtino_mouse.cpp discarding touch_port offsets: inputtino normalizes desktop coordinates,
but client_to_touchport returns capture-local coordinates. Adding the offsets was tested with the
same live setup: a client drag of (+360,+176) moved the host window from (2200,328) to (2560,504),
with both images inspected. This establishes scale1 positive-offset routing only; fractional
scaling, negative desktop origins and game placement remain open.

The graphical Flatpak launcher now offers input setup, with defer/retry and an Input Setup desktop
action. Denied host access and cancellation of the actual desktop polkit prompt both displayed
failure without creating the host rule/unit. First-run deferral and explicit reopening were also measured. Successful privileged setup,
autostart and uninstall still need validation. The service bypasses interactive setup.

## Linux custom encoder component checkpoint — 2026-10-12 12:50

The production portal capture adapter now loads the Linux 8/10-bit OpenCL libraries.
The library source lives in Eclipse experimental/gpu-hevc (see LINUX.md). It shares
Windows v12b picture/rate-control kernels; Linux currently uploads owned CPU BGRA
capture buffers. Capability discovery checks both libraries, kernels and an actual
GPU probe. Exactly one usable OpenCL GPU is required for now. No CPU encoder fallback.

Both libraries and the production host built against runtime25.08. Inside VEGA's
Flatpak sandbox, the manual portal-gpu-probe captured a fresh KDE desktop with
rotating gears for75s at each depth, streaming through a FIFO to a live decoder:
8-bit3375frames;10-bit3363frames; both failed=0/status=0 and forced IDR at120. Decoder
reported Main/yuv420p and Main10/yuv420p10le, limitedBT.709,1920x1088. Early/late
decoded images were inspected, with visible coloured gears and changed orientation.
Capture source1920x1080 was aspect-fit. Owned processes/FIFO cleaned and verified.
Logs/images: ~/umbra-flatpak/gpu-linux/. These are SDR component results, around45fps;
not GameStream/co-op, HDR, AMD, Windows parity, TV or latency qualification.

The development host uses a compile-time library directory override under the
test user's home. The installed portal-test Flatpak still predates this integration.
A self-contained source-built package, X11/KMS adapters, multi-GPU device selection
and full compatibility matrix remain release gates.

HDR source audit: KWin v6.6.6 OutputScreenCastSource::drmFormat returns ARGB8888;
ScreenCastStream's format table has no10-bit/float format. This is source evidence
for the current portal path's limitation, not runtime HDR validation. Sources:
https://github.com/KDE/kwin/blob/v6.6.6/src/plugins/screencast/outputscreencastsource.cpp
and https://github.com/KDE/kwin/blob/v6.6.6/src/plugins/screencast/screencaststream.cpp.
True HDR capture/conversion/metadata and valid SDR fallback remain open.

## Custom encoder transport/lifecycle checkpoint — 2026-10-12 13:16

Linux pane launch/resume now accepts custom sessions, requires32-pixel alignment,
and bypasses native encoder seam padding as Windows does. Custom sessions select
CPU capture explicitly; X11, wlroots RAM and KMS RAM capturers have adapters.
X11 rejects unsupported pixel masks/depth/byte order. The current CPU adapter
rejects HDR before opening an encoder, pending a real HDR capture conversion.
Full runtime25 host build passes; X11/wlroots/KMS execution remains unqualified.

Actual network tests used the installed Eclipse client protocol libraries and
live FIFO decoding (manual C probe in Eclipse tests/coop): 75s8-bit Desktop3393
frames;75s10-bit virtual pane2521frames, with an observed window move onto the
pane. Both received requested recovery IDRs and Quit succeeded. Main/Main10,
limitedBT.709, HDR=0; host and late decoded images inspected.

Disconnect/Resume repeated with correct diagnostic aspect handling:75s1920x1088
2919frames -> same app resumed75s1088x1920 3456frames; recovery in both phases;
late images showed correct wide/tall geometry. The host's virtual output matched
each requested size and Quit removed it, retainingDP-1. The original MJPEG preview
kept its initial landscape size and squashed portrait output; that preview was
ruled invalid and corrected, then the live run repeated. Host screenshot during
first portrait transition, matched landscape screenshot in the repeat, and final
post-Quit screenshot were inspected. Owned test processes/pipe cleanup verified.
Audio/input, mixed-host splicing, TV, HDR and performance parity remain open.

Packaging: module umbra-gpu-hevc builds two pinned PICx265 libraries and both
OpenCL encoder libraries from source, installing kernels/licences/provenance.
The module build passed underSDK25.08, exports only fourAGHL symbols per library,
and those built libraries were used in the network checks above. Vendored source
hashes identify Eclipse8c092dd6. A complete installed bundle check is still due.
Evidence: ~/umbra-flatpak/gpu-linux/ (network-8,network-10,network-resume plus host
logs). The previous failed geometry preview is retained separately.
