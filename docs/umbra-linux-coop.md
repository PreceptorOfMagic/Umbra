# Linux Co-op Pane: Flatpak capture feasibility

Status: **per-client virtual displays, GPU-frame capture and KDE co-op panes implemented, under validation; not released** (2026-10-13). Current candidate: see the Windows-parity section at the end.
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
4. Extend capture and conversion for HDR. The initial portal backend negotiated only BGRx/BGRA
   without HDR metadata. The production checkpoint below adds a validated GNOME DMA path;
   the native CUDA CPU-upload path still rejects P010.
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
  desktops leave that capability clear. This was the initial capture checkpoint; the Linux
  GPU-HEVC implementation and runtime evidence are described below.

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
X11 rejects unsupported pixel masks/depth/byte order. At this checkpoint the CPU adapter rejected HDR; the later portal-specific HDR
implementation below supersedes that restriction only for explicitly typed PQ frames.
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

## Production portal HDR checkpoint — 2026-10-12 15:05

The custom encoder's Linux ABI2 accepts explicit little-endian xBGR2101010
BT.2020/PQ input. Vendored source/provenance now pins Eclipse9d66834a. The host
requests HDR only for a custom10bit session, only after matching exactly one
Wayland output's logical position to the portal selection and obtaining complete
BT.2020/PQ target metadata. Optional protocol absence/ambiguity or GPU-import
failure leaves SDR available. Shared-memory packed10 is deliberately excluded:
GNOME50.1 advertised it but returned8bit pixels in the measured diagnostic.

The portal imports negotiated GPU buffers/modifiers into a capture-owned EGL
context, completes packed10 readback before returning the producer buffer, and
marks the owned image's color format explicitly. The custom adapter checks the
format before encoding. Other CPU capturers remain BGRA-only. Metadata queries
have a3s deadline, range-check GameStream fields, and watch output/topology/color
changes; these trigger capture/encoder reinitialization. No physical output is
changed by the host itself. The temporary mode switches below were test actions.

A full SDK25 host build and independently source-built ABI2 libraries were
assembled into the private `hdr-test` branch beside existing branches on headless
VEGA (Ubuntu26.04.1/GNOME50.1/NVIDIA595.99.02/RTX2060SUPER). This is an engineering
bundle, not a clean complete-manifest build or final acceptance release.
Live network tests used the actual installed host, client protocol libraries,
Wayland gray/RGB HDR pattern, and FIFO-fed live decoder at1920x1088:

| Run | Frames /75s | Recovery and client state |
|---|---:|---|
| HDR10 | 3069 | Initial + requested IDR; HDR enabled, valid target metadata, Main10 BT2020/PQ |
| SDR8 on HDR desktop | 3640 | Initial + requested IDR; HDR flags off, Main BT709 |
| HDR10 -> SDR10 desktop switch at35s | 3451 |3 IDRs; same connection, HDR-off notification, encoder reinitialized HDR0 |

All launch/Quit/diagnostic error results were0. Initial source screenshots and
late decoded images were opened: four gray bands, correctly ordered RGB patches,
advancing marker and correct aspect-fit. HDR gray Y511/574/638/725 and CbCr514
match the earlier component conversion, including its small inherited Windows
P010 scaling offset (not bit-exact luminance). SDR preserved distinct gray bands.
The switch decoder explicitly reported a change to BT709 and continued through
cleanup. Its fixed raw-video output retained its initial BT2020 matrix, so the
late raw preview alone is not evidence of the new stream's matrix; decoder
reconfiguration plus host/client flags supply that evidence. The initial HDR
notification can follow the first IDR (observed in the switch test).

Evidence: ~/umbra-flatpak/vega/gnome/network-{hdr,sdr,hdr-switch}-production/.
This verifies this GNOME/NVIDIA ordinary stream path and fallback, not TV HDR,
HDR games, controller/audio, mixed-host splicing, GNOME virtual-pane HDR, AMD,
or Windows visual/performance parity. Throughput remains below60fps. KWin and
other capture backends need their own runtime qualification; do not advertise
universal compositor HDR from these tests.

## GPU selection checkpoint — 2026-10-12 15:22

Linux ABI3 adds explicit GPU selection through the existing Graphics Adapter
setting. Empty selects the only eligible GPU or the single non-unified-memory
GPU in a hybrid system; this is a topology policy, not a performance ranking.
DRM nodes are resolved through their character-device sysfs identity and matched
against cl_khr_pci_bus_info. Unique exact OpenCL names work without that extension.
Unknown/ambiguous choices fail with available identities; no silent replacement.
Both availability discovery and encoder creation use the same selection.

SDK25 builds passed. Both8/10bit libraries passed real Flatpak GPU probe/open
checks for auto/name/PCI and rejection of missing devices. Policy fixtures cover
hybrid ordering, duplicate names/PCI, unavailable choices and single integrated
GPU. This is not physical dual-GPU qualification.

Packaged live network10bitSDR check on Plasma/NVIDIA, adapter_name=/dev/dri/renderD128:
3365frames/75s,2IDRs, requested recovery, start/cancel/errors0. Host reported RTX2060SUPER
pci:0000:01:00.0. Main10 limitedBT709. Source and two decoded images showed correct
desktop/gears colors, geometry and changed gear positions. All owned processes
stopped after completion. Evidence:~/umbra-flatpak/gpu-linux/network-selection/.
At this checkpoint HDR runtime evidence applied to ABI2. The ABI3 HDR regression
below closes that version gap; AMD and physical multi-GPU remain unqualified.

## Mint X11 startup and live stream — 2026-10-12 15:58

Linux Mint22.3/Cinnamon6.6.2, signed kernel7.0.0-38, NVIDIA580.178.04,
Secure Boot enabled, headless1920x1080. Steam retained the transferred login and
native Children of Morta reached its title screen; no save was selected.

The packaged host hung twice during X11 display enumeration, including with the
game closed. A live backtrace showed the tray thread inside GTK initialization
waiting in XSetErrorHandler while the main thread waited for an XOpenDisplay
connection reply. Other X11 clients also stalled; stopping Umbra released them.
Linux tray startup now finishes (success or failure) before platform probing
continues. GTK remains on its original tray thread. The updated full host passed
three subsequent startup/HTTP/desktop-response checks, plus a live Main10 SDR
custom-encoder stream:4291frames over75s,2IDRs,requested recovery1,launch/Quit0,
failed0. Explicit renderD128 selected RTX2060SUPER pci:0000:01:00.0. Source and
received desktop/game-loading images were inspected. This was the rebuilt binary
inside the installed Flatpak runtime, not yet a newly exported final package.
Evidence: private ~/umbra-flatpak/vega/dual-distro/network-mint and startup logs.

This closes the measured Mint startup deadlock. The earlier WSL/Xvfb heap abort
has not yet been rerun and is not established as the same defect. Native input
permissions, controller gameplay, decoded audio, TV presentation, AMD and X11
virtual panes remain unqualified. This is a one-host smoke, not co-op acceptance.

## Mint input-device first setup and reboot — 2026-10-12 16:26

Mint had a static /dev/uhid node without a registered /sys/class/misc/uhid device.
The old helper checked only /dev existence, skipped modprobe, and then its udev
trigger could not grant access. Loading uhid and triggering its real device gave
the logged-in user read/write ACLs and passed access checks inside the Flatpak.
The helper now explicitly loads both modules. The uhid rule also gets
static_node=uhid, matching the existing uinput approach, so static nodes receive
the rule's permissions and uaccess tag at boot. After Mint -> Ubuntu -> Mint,
both nodes had camer:rw ACLs and were writable inside Flatpak before uhid loaded.
Host encoder/input probing created Xbox, DualSense and Nintendo virtual devices.
The first-run input UI correctly returned without a prompt when access was ready.
Rules were installed through authorized SSH sudo for this headless diagnostic;
the GUI's administrator-password success path remains unqualified.

A live input-mode diagnostic sent right-hat/stick16384/trigger64 and releases.
The exact corresponding host evdev events were observed; its virtual Xbox device
was removed on Quit. Main8 live stream4336frames/75s,2IDRs/recovery1,clean launch/
Quit. This proves synthetic network-to-virtual-controller input, not a physical
pad, game mapping or co-op assignment. Mouse enqueue succeeded but independent
mouse delivery was not measured. A separate default capture/GPU-selection run
passed4361frames/75s with a deliberate Steam-window movement visible in received
images. OS package updates ran concurrently; these are not performance comparisons.
The no-desktop tray failure path also reached the configuration HTTP endpoint
without deadlocking, then shut down. Evidence: private vega/dual-distro tree
under ~/umbra-flatpak (network-input, input-events.log, network-auto,
mint-return-boot.log, mint-tray-failure.log).

## Packaged ABI3 HDR regression — 2026-10-12 16:51

Ubuntu26.04.1/GNOME50.1/NVIDIA595.99.02, headless RTX2060SUPER. Installed
hdr-test artifact a7a785ea0d873fb1e6ef69ddb42c368a9edf75f60e7779a7abed90c329fc4bc9
contains the full host with the X11 startup fix and both ABI3 encoder libraries.
Each diagnostic used real network encoding for75s and a FIFO-fed live decoder.

| Check | Frames | Result |
|---|---:|---|
| HDR10,1024x1024 |3322|DMA-BUF HDR/PQ; selected RTX2060SUPER PCI0000:01:00.0; valid target metadata;2IDRs/recovery1|
| SDR8 on HDR desktop,1920x1088 |3705|HDRoff,MainBT709;2IDRs/recovery1;full grey/RGB pattern visible|
| HDR10 -> SDR10 at35s,1920x1088 |3399|Same connection;encoder reopened HDR0;client HDRoff;3IDRs/recovery2|

All three completed launch/Quit with failed0. Source and late decoded images
were inspected: four grey steps, bottom RGB/white patches and the moving marker
remained visible with correct aspect fitting. HDR grey samples decoded to
approximately102,206,410,1025nits for intended100,203,400,1000nit patches. This
retains the previously documented shared conversion scaling discrepancy; it
is not bit-exact colour preservation or Windows colour-parity evidence.
The switch preview retains FFmpeg's initial BT2020 output conversion; its
appearance alone does not establish the new stream's colour metadata.

The first SDR attempt outlived the pattern's120s timeout and ended on the
desktop. It is retained as transport evidence only and superseded for pattern
qualification by network-sdr-abi3-pattern. Restart the bounded host/pattern
harness for each75s test. Evidence:~/umbra-flatpak/vega/gnome/network-hdr-abi3,
network-sdr-abi3-pattern and network-hdr-switch-abi3. Cleanup restored SDR and
Plasma; no owned host/pattern/decoder remained. No TV, HDR-game, co-op, AMD,
physical multi-GPU, latency or sustained60fps verdict follows from these checks.

## Windows shared-header regression — 2026-10-12 16:56

Both Windows host libraries rebuilt with MSVC2022 /W4 /WX against the original
pinned Windows x265 dependencies. No deployed Windows binary was replaced.
The only changed shared source file versus gpu-hevc-v12/project/src is the
RPS constructor initialization replacing memset. A configuration-only manual
contract test compared reference/candidate parameter sets, slice bytes and
160 CABAC contexts byte for byte:1152 cases at8bit and2304 at10bit, including
HDR, both pane geometries, QP extremes, intra/inter and POC wrap boundaries.
Both comparisons matched completely. This closes compilation and syntax
regression coverage for that change; live Windows GPU/frame, mixed-host and
performance parity are still separate gates. Evidence: private
C:/Users/camer/umbra-linux-compat-20261012/windows-encoder/header-contract.

## Acceptance fixes — 2026-10-12 21:15

The user's TV run against the earlier candidate (bundle SHA256
714f5e2a36ac6f215d072db85ab017c99e31bbe149ba8829998c635f45c7406f) failed twice: an HDR
request ended in a network error, and starting co-op showed a desktop approval prompt. That
bundle is superseded and must not be delivered. Fixes on `linux/coop`:

- `138452e2`: a client that requests Rec2020 while the captured desktop is SDR. The custom
  converter rejected BT2020 SDR, no frame was produced and the client reported a network error.
  That path now converts and signals Rec709; true HDR capture remains HDR. Visible stream sizes
  can be any even value from 64 to 8192: the coded frame is padded to 32 and the SPS signals a
  conformance crop.
- `e821638d`: the KDE co-op pane uses KWin's native `zkde_screencast_unstable_v1` virtual output.
  The desktop file declares the interface and the Flatpak gets the user PipeWire socket, so the
  portal picker never appears for co-op. KWin's normal per-application authorization applies;
  there is no global permission bypass. Dispatch waits are bounded to 3s and Quit destroys the
  output. Ordinary desktop capture still uses the portal and may need first-use monitor consent.
- `73010ad9`: allow mode changes on Umbra-owned native output names. The old guard only accepted
  the portal prefix and rejected the first native pane.
- `9ed871b0`: recipe pins `73010ad9` and adds `--filesystem=xdg-run/pipewire-0`.

Candidate `Umbra-0.5.1-linux-candidate-x86_64.flatpak`, SHA256
9cc288f793423b4ea42f94088b2990d719e799ad5ad0ac224345759fe2714131 (identical to
`Umbra-0.5.1-acceptance-fixes-2-x86_64.flatpak`); installed `/app/bin/sunshine` SHA256
3b3cc8433b1dbbb1e22ec9646a7097c3b3672f2ec988675355ba1f3f5eead1a9, commit a8259f093ce2.

Live checks on VEGA (Ubuntu 26.04.1, RTX 2060 SUPER, 595.99.02): real-time host encoding into a
network probe and FIFO decoder. Each phase ran 25s at a requested 60fps. Source and decoded images
were opened and read.

| Run | Build | Frames | Result |
|---|---|---:|---|
| KDE SDR, 3840x2160 Main10, Rec2020 request | old 714f | 0 | Reproduced the colour rejection (expected negative) |
| Same request | e821 | 1090 | BT709 decode at exact size, correct proportions; 2 IDRs, recovery 1 |
| Custom Main8 1918x1078 | e821 | 1138 | Exact size, no padded strip or stretch |
| Native KDE pane, HEVC8 1920x2176 | e821 | — | No prompt, but the mode guard rejected it; fixed in 73010 |
| Native KDE pane, HEVC8 1920x2160 | 73010 | 291 | No prompt, exact mode, Quit removed the output |
| Native KDE pane, custom Main8 1920x2176 | 73010 | 785 | No prompt; moving gears in source and decode; Quit removed the output |
| Native pane Disconnect/Resume, 1920x2176 -> 2176x1920 | 73010 | 1720 | 4 IDRs, recovery 2; lifecycle without a picker |
| Same, resumed-aspect visual (21:12) | 73010 | 1712 | Decoder reconfigured to 2176x1920; letterboxed landscape with round gears matching source window position |
| GNOME automatic 3840x2160 Main10, true HDR | 73010 | 941 | HDR enabled, valid metadata; PQ pattern layout and aspect correct |

All runs reported launch, cancel and failure counts of 0. Evidence:
`~/umbra-flatpak/gpu-linux/acceptance-*` and `~/umbra-flatpak/vega/gnome/network-fixed-hdr-4k`.
These are one-host checks, not TV presentation. They are not verdicts on mixed-host co-op, 165fps,
latency or calibrated HDR. Linux virtual co-op is SDR. TV, co-op, game, pad, audio, AMD/multi-GPU and Windows-parity acceptance are
still outstanding.

### Mint 22.3 / Cinnamon X11 — 2026-10-12 21:23

The same bundle was installed as `master` on Mint 22.3 (NVIDIA 580.178.04, same RTX 2060 SUPER).
Installed commit a8259f093ce2 and `/app/bin/sunshine` SHA256 3b3cc843… match Ubuntu. The X11
NVIDIA capture path is NV12-only, so on Mint the host advertises H.264 and HEVC Main8 but not
Main10 (`server_codecs=0x101`). An automatic Main10 request is therefore refused before connect.
The custom GPU encoder still provides Main10.

| Run | Frames | Result |
|---|---:|---|
| Automatic HEVC Main8 3840x2160, Rec2020 request on SDR desktop | 1473 | hevc_nvenc; host logged the stock "BT.2020 SDR expects 10-bit, falling back to Rec. 709"; decoded BT709 at exact size; 2 IDRs, recovery 1 |
| Custom GPU HEVC Main10 3840x2160, SDR | 1145 | `[AURORA-GPU-HEVC] Linux opened 3840x2160 depth=10 HDR=0` on the RTX 2060 SUPER, CPU capture upload; decoded Main 10 BT709; 2 IDRs, recovery 1 |

Both runs reported launch, cancel and failure counts of 0. Source screenshots and decoded frames
were read: the Mint desktop at correct 16:9 proportions, with moving gears and correct primaries.
Each run had its own bounded host and gears harness, torn down afterwards. Evidence:
`~/umbra-flatpak/gpu-linux/network-mint-accept-*`; host logs are in VEGA's Mint home as
`umbra-coop-probe/mint-accept-{auto-sdr2020-8,sdr-10}-host.log`. The automatic run does not exercise the
custom converter fix; the TV's Rec2020 case on KDE is covered by the Ubuntu rows above.

## Windows-parity display and encoding — 2026-10-13 00:40

The user requirement: Linux Umbra streams exactly like Windows Umbra. The reference is the Vibepollo
edition's defaults (`virtual_display_mode=per_client`, `virtual_display_layout=exclusive`): every
stream gets its own virtual display at the client's width, height and rate before the app starts,
it is the only active display, and ordinary streams use the native encoder from GPU memory.

Before this work an ordinary TV session on KDE captured the 1920x1080@60 physical output and
upscaled it to the requested 3840x2160. The host forced the GPU HEVC co-op encoder because the TV
asked for HDR (`resolve_desktop_hdr_encoder`). The result was ~44 fps with one host thread at 98%.

Commits on `linux/coop`:

- `4e5f5fb5`: every KDE stream creates its KWin virtual output at the client's WxH@fps first
  (process.cpp). It is made the only enabled output (`umbra_kde_output_exclusive`). Outputs it
  switches off are recorded in appdata `kde_exclusive_outputs` before any change. They are
  re-enabled on release and at host start. Resumed ordinary sessions re-fit the display. The
  portal-HDR GPU HEVC route is off where KDE virtual displays are used: KWin captures them 8-bit,
  so Automatic stays on the native encoder.
- `b6913de5`, `73e0623e`: PipeWire DMA-BUF in EGL-importable modifiers for NVENC (compositor GPU
  is NVIDIA) and VA-API. The newest buffer stays checked out and the existing VRAM encoders
  (`gl_cuda_vram`, VA-API vram) get duplicated fds. CPU buffers remain the fallback, and a failed
  attempt never tears down the virtual display. Each failure point logs its reason.
- `cf8c74a0`: where streams use native virtual displays, the encoder probe uses a capture-free
  display (dummy images only). Host start never opens the screen-sharing prompt.
- `43777e07`: the pointer is centred on the virtual display after launch and resume (KDE's
  top-left hot corner otherwise opened Overview). `[UMBRA-CAPTURE]` logs compositor arrivals
  against encoder deliveries every 10s.
- `a8e846b2`: 4 PipeWire buffers when one is held. `50ab1f74`: GPU HEVC sessions take DMA-BUF and
  read it back on the capture thread. `2462bc48`: Mesa rusticl is enabled for `radeonsi,iris` unless
  the user set `RUSTICL_ENABLE`. `0e0c61a6`: where streams capture KDE virtual displays, native codecs are
  advertised 8-bit only (Main10 from an 8-bit capture measured 88 fps vs 144; the mask had also changed
  after the first stream). `0e0c61a6` is the final code; the recipe pins it.

Final candidate `Umbra-0.5.1-parity-final2-x86_64.flatpak` (= `Umbra-0.5.1-linux-candidate-x86_64.flatpak`),
SHA256 b34709564defcf856018541de4904110c6c5e483d14fcd5fcccd7560719a9da5;
`/app/bin/sunshine` f74389a774e49634…, installed on VEGA Ubuntu 26.04/KDE and Mint 22.3. V0-V5 ran on
the preceding build (code 2462bc48); the final build repeated V0, V1 (3616 frames), V4 (1683) and Mint (1477) with
codecs 0x101 before and after streams.

Live checks on VEGA (RTX 2060 SUPER, 595.99.02). Each was a real-time host encode into a network
probe and live decoder at a requested 165 fps. Source and decoded images were read.

| Check | Result |
|---|---|
| V0 host start | Probe display, no prompt; NVENC H.264/HEVC found in <0.5s |
| V1 Desktop 3840x2160@165, Automatic, Rec2020 request | Virtual 3840x2160@165000 mHz, DP-1 disabled during / enabled after; DMA-BUF GPU frames -> hevc_nvenc; KWin arrived 1655/10s, every frame delivered; client 3610/25s; host CPU ~15%, NVENC ~71%; decoded == source, pointer centred, no Overview |
| V2 Desktop 1920x1080 -> Disconnect -> Resume 1080x1920 | 3595 + 3624 frames; resize on resume, GPU frames both phases; portrait desktop decoded correctly |
| V3 Co-op pane 1920x2160, Automatic (NVENC) | 3609/25s; whole-desktop pane with taskbar |
| V4 Co-op pane GPU HEVC 1920x2176 -> Resume 2176x1920 | 1627 frames, 4 IDRs, failed 0; DMA-BUF read back; ~42 frames/s, every frame encoded; GPU HEVC 17-25 ms/frame |
| V5 SIGKILL mid-stream | KWin re-enabled DP-1 within 3s on its own; restarted host: no prompt |
| Mint 22.3 X11, Automatic 3840x2160 | 1484/25s (60 Hz physical X11 output); X11 has no virtual displays |
| V6 Main10 Automatic after a warm-up stream (2462bc48) | Worked as 10-bit SDR but 88 fps; fixed by 0e0c61a6 (8-bit only on KDE virtual displays) |

Eclipse's Automatic co-op plan uses each host's native encoder for NVIDIA+NVIDIA or unknown-vendor
pairs, and GPU HEVC only for mixed vendors (`coop_stream_plan_choose`). A PC1 (RTX 4070) + VEGA
(RTX 2060 SUPER) co-op therefore takes the V3 path.

Hard Linux limits measured or read from upstream source (for the website, later):

- HDR co-op panes: KWin's screencast offers only 8-bit formats plus NV12 (KWin master
  `src/plugins/screencast/screencaststream.cpp`, fetched 2026-10-12). A GNOME 50.1 Mutter
  `RecordVirtual` monitor reports `supported-color-modes [0]` (measured), while the physical DP-1
  reports `[0, 2, 1]`. Linux virtual panes are SDR on both desktops; `UmbraCoopHdr=0` keeps the TV
  pair SDR.
- KDE ordinary streams are SDR, for the same KWin reason. GNOME ordinary HDR still works by
  capturing an HDR-mode physical monitor.
- X11 (Mint/Cinnamon) has no per-client virtual display. Streams run at the physical output's mode.

Superseded open items: see the GNOME section below (GNOME virtual displays done, version label
fixed, co-op zero-copy measured as an NVIDIA driver limit; AMD still unmeasured).

## GNOME per-client virtual displays and final candidate — 2026-10-13 06:50

GNOME now gets the same per-client exclusive virtual display as KDE, with no prompt. Mutter
ScreenCast `RecordVirtual` (interface v4) creates it. A short PipeWire negotiation sets the
client's WxH and max-framerate, and the result is verified and made exclusive through a temporary
`DisplayConfig.ApplyMonitorsConfig` (method 1). Mutter ends the session with the host's bus
connection, so a crash restores the physical layout. The Flatpak gained
`--talk-name=org.gnome.Mutter.ScreenCast` and `--talk-name=org.gnome.Mutter.DisplayConfig`.

Commits: `03bc7f1d` (GNOME backend, KDE-only rules split out, `BUILD_VERSION v0.5.1-linux-parity`);
`74403bd0` (PipeWire init before the first sizing stream); `ec62f67d` (one GPU-frame renegotiation
before the CPU fallback). The first negotiation after a mode change can return a linear DMA-BUF on
both KWin and Mutter, and this NVIDIA driver cannot import it.

HDR on GNOME, capability-driven: Mutter virtual monitors are SDR (colour modes [0]). The host
advertises Main10 and takes the existing physical-monitor HDR route only while a physical monitor
is in HDR mode. Otherwise GNOME streams use 8-bit per-client virtual displays, like KDE.

Final candidate `Umbra-0.5.1-gnome-3-x86_64.flatpak` (= `Umbra-0.5.1-linux-candidate-x86_64.flatpak`),
SHA256 d5f0a14e2039d7fa76d55be093d85735d8f599a46956b4dd8e1c6cc0a4244e2b; `/app/bin/sunshine` ce36deb03675d845…; version 0.5.1-linux-parity.

Live checks on VEGA (RTX 2060 SUPER). Each was a real-time encode into a network probe and live
decoder with images read.

| Check | Result |
|---|---|
| GNOME 50.1 G1 Desktop 3840x2160@165 Automatic | Meta-0 3840x2160@165.000 only active display; GPU frames -> hevc_nvenc; Mutter delivered 1645/10s; client 4076/25s (163 fps); DP-1 back after Quit; top bar + gears decoded correctly |
| G2 1920x1080 -> Resume 1080x1920 | 4093 + 4100 frames; the resize renegotiated once (linear) then ran on GPU frames at 165/s |
| G3 co-op pane NVENC 1920x2160 / GPU HEVC 1920x2176 -> 2176x1920 | 4107 frames / 1669 frames (~34 fps, encoder-bound) |
| G4 SIGKILL mid-stream | DP-1 restored within 3s by Mutter; restart: no prompt |
| G5 DP-1 in HDR mode, Automatic HDR request | Advertised 0x301; physical capture DMA-BUF HDR/PQ -> GPU HEVC 10-bit HDR=1; client HDR on, valid metadata; DP-1 returned to SDR afterwards |
| KDE regression (same build) | V1 3571 frames exclusive; resume to 1080x1920 on GPU frames; co-op GPU HEVC resume 1617 frames |
| Mint 22.3 X11 | 1469 frames; X11 unchanged (UmbraCaps 2) |

Co-op encoder zero-copy, measured on VEGA (NVIDIA 595.99.02, OpenCL 3.0): no
`cl_khr_external_memory_dma_buf`, and `clCreateContext` sharing an EGL GL context returns -59. A
DMA-BUF therefore cannot reach the OpenCL encoder without a CPU copy on NVIDIA. The adapter's
remaining copy is required: the encode loop re-encodes the last converted frame when no new
frame arrives (`video.cpp` `frame_captured`), after the capture image may have returned to its pool.

Hard Linux limits (for the website, later): HDR co-op panes (KWin screencast 8-bit; Mutter
virtual monitors SDR-only). KDE ordinary streams are SDR. X11 has no per-client virtual display.
NVIDIA OpenCL cannot take DMA-BUF zero-copy.

Unmeasured: anything AMD (VA-API GPU frames, rusticl OpenCL for GPU HEVC, AMD+NVIDIA co-op). This
needs the RX Vega back in VEGA.

## KDE HDR and GPU-frame co-op — 2026-10-13 15:20

Host candidate: Flatpak `build25-hdr5`, Umbra `d162fd63` (`v0.5.1-linux-parity`), on VEGA
Ubuntu 26.04 KDE with KWin `4:6.6.6-0ubuntu0.1+umbrahdr4` (`packaging/linux/kwin-virtual-hdr/`).

**KDE HDR.** Each stream's virtual output is switched to HDR when the client asks for HDR and
KWin offers it (capability 0x8, detected; stock KWin keeps SDR). KWin reports an HDR output by
its blending space (BT.2020 + gamma 2.2), so Umbra trusts its own confirmed switch
(`session_t::virtual_hdr`) and captures 10-bit PQ. Measured: Automatic HDR 3840x2160@165 ->
NVENC Main10, client HDR on with BT.2020/PQ metadata, ~161 fps; co-op pane 10-bit HDR ~157 fps.
A custom-encoder HDR desktop stream now keeps its virtual display instead of the GNOME
physical-monitor rule (which waited on a screen-sharing prompt).

**GPU frames for GPU HEVC (co-op).** As Windows hands the encoder its D3D11 texture, the co-op
encoder now takes the capture's DMA-BUF (`src/platform/linux/gpu-hevc-frames.{h,cpp}`): one GL
draw packs it into the encoder's input words and, on NVIDIA, CUDA copies it into a VMM allocation
the OpenCL encoder imports by opaque fd (`AGHL_FRAME_EXTERNAL`). No host copy. Measured co-op pane
1920x2176@165: KDE 3574 frames per probe window (~160 fps; read-back gave 791, ~32 fps), GNOME
4062 (165 fps); encoder submit-to-packet ~5 ms (was ~17-18 ms with read-back and upload). Resume
at a new size (1920x2176 -> 2176x1920) passes; colours and orientation verified against the
desktop on KDE and GNOME. Fallbacks, all logged: read-back of the packed texture (no CUDA, another
GPU, a failed copy), host pixels for one frame if the encoder cannot import, and the old read-back
capture if the GL stage fails.

Not measured: AMD (no AMD GPU fitted). On AMD the GL stage and read-back hand-off apply; a
zero-copy AMD hand-off (Mesa rusticl/ROCm import) is still to be written.

## AMD GPU-frame path (rusticl GL sharing) — 2026-10-13 15:45

Candidate `build25-hdr6`, Umbra `1f63fa11`. Mesa rusticl (AMD/Intel OpenCL in the Flatpak runtime)
has `cl_khr_gl_sharing` but no external-memory import, so AMD cannot use the NVIDIA CUDA hand-off.
Where CUDA does not hand frames over, the encoder session's OpenCL context now shares the GPU-frame
stage's EGL context and reads the packed texture directly (`AGHL_FRAME_GL_TEXTURE`); otherwise frames
are read back. Measured without an AMD card (details and probes: `packaging/linux/gpu-frames-probes/`):
the GPU HEVC encoder runs on rusticl (zink over the RTX 2060 SUPER) and decodes within 2/255 of its
source; the rusticl GL-sharing sequence is bit-exact; an import failure returns
`AGHL_EXTERNAL_UNAVAILABLE` and the resubmit is byte-identical. Live on NVIDIA with
`UMBRA_GPU_FRAMES_CUDA=0`: GL sharing is negotiated, NVIDIA refuses it, read-back hand-off ~46 fps
(old capture read-back ~32). CUDA path after the change: co-op 8-bit 3563 frames, 10-bit HDR 3493.
Owed: one run on an AMD GPU (radeonsi exporting the texture to rusticl).
