# Linux host library — development status, 2026-10-12

The Linux OpenCL adapter shares the Windows v12b picture encoder, rate controller,
HEVC headers and eight existing kernels. Before this port, all nine source files
matched the v12 build source and all eight kernel hashes matched the release's
BUILD-PROVENANCE.json. Linux has a separate ABI (AGHL version3; earlier gpu-test/HDR checkpoints use1/2); Windows ABI3 is
unchanged. The shared RPS reset now uses its constructor, which initializes every
field, instead of memset, to compile with GCC's class-memaccess warning enabled.
A Windows rebuild/regression check remains due.

The Linux adapter uploads CPU capture pixels to a GPU image. Conversion, aspect
fitting and picture encoding run through OpenCL. There is no CPU picture-encoder
fallback. Automatic selection uses the only eligible GPU, or the single GPU without
unified host/device memory in a hybrid system. Otherwise explicit selection is
required. The output libraries export only the five AGHL entry points; their private x265 symbols cannot interpose with each other or the
host's x265 dependency.

## Build

Use the x265 and Khronos header pins in README.md. Build separate static x265
libraries with HIGH_BIT_DEPTH=OFF/ON, ENABLE_PIC=ON, ENABLE_SHARED=OFF,
ENABLE_ASSEMBLY=OFF and ENABLE_CLI=OFF. CMake 4 needs
CMAKE_POLICY_VERSION_MINIMUM=3.5 for this pinned x265 source.

Configure this directory with BUILD_LINUX_HOST_LIBS=ON, OPENCL_HEADERS,
X265_SOURCE, X265_BUILD_8 and X265_BUILD_10 pointing at those source/build trees.
Build targets aurora_gpu_hevc8 and aurora_gpu_hevc10. Installation places both
libraries and kernels in lib/umbra/gpu-hevc. OpenCL must be usable inside the
application's sandbox, not merely on the native host.

## Live component evidence

VEGA remained headless: Ubuntu 26.04.1, Plasma 6.6.6, NVIDIA 595.99.02,
RTX 2060 SUPER; Freedesktop runtime 25.08 Flatpak sandbox. Production Umbra
adapter built successfully. Its manual portal-gpu-probe captured the live KDE
desktop and rotating glxgears, then sent encoded output through a FIFO to a live
FFmpeg decoder. No recording supplied the encoder input.

| Check | 8-bit SDR | 10-bit SDR |
|---|---|---|
| Duration after initialization | 75 seconds | 75 seconds |
| Encoded frames | 3375 | 3363 |
| Decoder format | HEVC Main, yuv420p | HEVC Main 10, yuv420p10le |
| Colour / coded size | limited BT.709, 1920x1088 | limited BT.709, 1920x1088 |
| Requested recovery at frame 120 | IDR observed | IDR observed |
| Probe completion | failed=0, status=0 | failed=0, status=0 |

Early/late decoded images were opened and inspected: visible KDE desktop and
red/green/blue gears, changed gear orientation, no black/corrupt image observed.
The 8-bit host image was also inspected. Actual capture was 1920x1080, aspect-fit
to the aligned output. Latest JPEG timestamps advanced through each test.
The owned Flatpak, gears, decoder and FIFO were removed at teardown, verified by
process inventory. Logs and images are under ~/umbra-flatpak/gpu-linux/.

This is roughly 45 encoded frames/second in this component setup, not proof of
requested 60 fps, game performance, end-to-end latency, visual parity with Windows,
GameStream, two-host splicing, AMD/Intel, HDR or TV presentation. Ten-bit SDR is not
HDR. The library accepts linear scRGB half-float for HDR conversion, but the
production portal adapter currently accepts only BGRA SDR; HDR qualification is
still open. Neither this directory nor the old installed Flatpak is a final
acceptance candidate.

## Network and lifecycle checkpoint — 2026-10-12 13:16

Using the installed Eclipse GameStream/moonlight-common-c libraries with the
manual diagnostic in tests/coop/linux_gpu_stream_probe.c, the same headless host
completed real network streaming with the source-built package-module libraries:

- 8-bit Desktop, 1920x1088, 75s: 3393 received frames, recovery IDR after request,
  launch and Quit returned0; Main/limitedBT.709 decoded and late pixels inspected.
- 10-bit virtual pane, 1920x1088, 75s: 2521 received frames (initially idle),
  requested recovery received, launch/Quit0; Main10/limitedBT.709, HDR notification0.
  A window moved from DP-1 onto the virtual pane appeared in the decoded image;
  host and late client images inspected. Quit removed the virtual output.
- 8-bit Disconnect/Resume, 75s per phase: 1920x1088 (2919 frames), then1088x1920
  (3456 frames), requested recovery received in each phase. Same app remained
  active between phases. Final Quit removed the virtual output and kept DP-1.
  Late landscape/portrait previews inspected with correct window proportions;
  landscape host image and post-Quit host image inspected. A separate first run
  supplied a host screenshot during the portrait transition.

The first resume test's portrait JPEG was squashed by the diagnostic's fixed
MJPEG output size after the HEVC decoder reconfigured. It is not valid geometry
proof. Repeated live testing with a fixed960x960 aspect-preserving preview canvas
resolved this: portrait pixels have correct proportions. The earlier records are
kept under network-resume-preview-invalid. A startup-race abort also exposed a
FIFO-reader cleanup hang; the diagnostic now kills its own blocked decoder and
cleans the pipe on failure.

Network logs/images: ~/umbra-flatpak/gpu-linux/network-{8,10,resume}/,
network-host.log, resume-host.log. Both custom host depths logged HDR=0. The
transport diagnostic discards audio and does not test input, TV, actual two-host
splicing, high-rate/game contention or Windows parity. CPU X11/wlroots/KMS adapters
now compile but have not been runtime-qualified; HDR capture is still rejected by
the CPU adapter. These remain development checkpoints, not release acceptance.


## GNOME single-host SDR checkpoint (2026-10-12 13:40)

Installed alongside Plasma on the same headless Ubuntu26.04.1 host: GNOME Shell
50.1, Mutter50.1 and xdg-desktop-portal-gnome50.0, with the same NVIDIA595.99.02
and gpu-test Flatpak. Ordinary portal consent was approved through its GUI.
The packaged custom8-bit encoder streamed the real desktop at1920x1088 for75s:
3231 received frames, two IDRs including the requested recovery, start/cancel0,
failed0. Host capture and late live-decoded images were opened and showed the
coloured moving gears with matching geometry. Logs/images are under
~/umbra-flatpak/vega/gnome/network-sdr/. This qualifies neither GNOME co-op nor HDR.

Mutter50.1 source advertises BT2020/PQ monitor capture in xRGB210LE, xBGR210LE and
RGBA_F16 when the monitor colour state is HDR. Its virtual-stream class inherits
SDR-only formats. The headless DP-1 exposed colour modes0/2/1, and a temporary
mode1 change read back successfully, then restored0. The HDR capture diagnostic
was initially stopped before consent to let the user sign in to Steam.
The14:32 capture checkpoint below supersedes that initial lack of HDR evidence. Runtime25 SDK PipeWire1.4.9 lacks the
SMPTE2084 enum name added in PipeWire1.6, though its wire value14 is source-defined.


GNOME application-capture issue (OPEN,13:52): after the above gears smoke, native
Mutter/GStreamer snapshots of Steam sometimes contained complete windows and
sometimes mostly black areas with isolated controls. Steam was signed in, but the
game installation could not be confirmed through that UI. Root cause is not
established: these images do not distinguish compositor capture from application
rendering. XWD could not provide an independent comparison (BadColor). This is a
release gate for GNOME application testing, not a passed general-desktop result.
The native diagnostic is tests/coop/mutter_live_screenshot.py; it captures live
outputs only, closes its own Mutter session/decoder, and is separate from Flatpak
portal acceptance. Plasma was restored to finish the game preparation.

Steam/headless setup follow-up (14:05–14:07): login survived a full Steam restart
and return to Plasma. Children of Morta330020 installation started at14:05 after
an evdev absolute mouse (BTN_LEFT + ABS_X/Y, INPUT_PROP_DIRECT) reached the visible
Install control. Earlier xdotool/ydotool mouse attempts did not start installation;
command exit and XWayland pointer coordinates were insufficient evidence. The
successful capture shows a complete Steam Store page and Starting Download.
A diagnostic launch with Valve's `-cef-disable-gpu` is still in use, but did not
alone resolve the incomplete snapshots. Do not attribute the improvement to it.

The headless machine's old PowerDevil configuration was also wrong: timeout0
with no WhenIdle=false does not disable screen power saving. PowerDevil6.6.6
DPMS source clamps that timeout to30s, while AutoSuspendAction1 means Sleep.
Corrected all profiles to DimDisplayWhenIdle=false, TurnOffDisplayWhenIdle=false,
AutoSuspendAction0 and LidAction0; restarted PowerDevil at14:06:42. This is a test
machine setup correction, not a change to Umbra defaults. A capture after34s idle stayed awake without forcing DPMS; a later DRM
readback also reported On. Longer idle remains to qualify. The general Steam
capture issue is not yet closed. Source audit is ~/umbra-flatpak/vega/power-audit/.

Children of Morta native Linux installation completed14:07 (game1.74GB, plus
Scout/Soldier runtime and shader cache; root1.5GB free). First launch rejected
old FMOD libraries' executable-stack flags. Backed up libfmod.so/libfmodL.so under
VEGA ~/umbra-coop-probe/morta-fmod-original-20261012/ and cleared only GNU_STACK's
execute bit with patchelf: exactly one byte each at offset348 changed7->6. The
retry loaded FMOD, created a running/unmuted FMOD Audio sink input and reached
the title/save-management UI; no save selected or overwritten. These local game
file changes are separate from Umbra and may be overwritten by Steam updates.

Packaged custom8-bit HEVC game-menu smoke14:11:08–14:12:23: 75s,3376 frames,
2 IDRs,1 requested recovery,start0/cancel0/failed0. Fresh host and late decoded
images showed the coloured title and mode-selection UI with correct proportions;
minimal native keyboard Enter visibly navigated to save management. No TV,
controller, decoded audio, gameplay, HDR or mixed-host proof is claimed. Evidence:
~/umbra-flatpak/vega/gnome/network-morta/. Host/receiver stopped and game exited
through its own menu. This is approximately45fps at1920x1088, not60fps parity.

## HDR capture checkpoint — 2026-10-12 14:32

A source-built Wayland application (`tests/coop/wayland_hdr_pattern.c`) submits
BT.2020/ST2084 grayscale bands at100/203/400/1000nits with a moving black marker.
The source10-bit codes are520/594/668/769. GNOME50.1 accepted the parametric
image description. The headless monitor was temporarily set to HDR and restored
to SDR by the wrapper's exit trap; the pattern and probe exited.

The first ordinary Flatpak portal capture negotiated xBGR210LE, BT2020/PQ and
received1200 frames without corrupt flags, **but failed the pixel contract**:
shared-memory bytes were8-bit BGRA (130/148/166/192 gray, alpha255), not packed10.
Mutter50.1's monitor `record_to_buffer` calls `clutter_stage_paint_to_buffer`
with `COGL_PIXEL_FORMAT_CAIRO_ARGB32_COMPAT` regardless of negotiated pixel format.
Do not trust the negotiated bit depth or reinterpret these buffers as packed10.

The DMA-BUF diagnostic (`tests/coop/portal_hdr_probe.c` plus helper header) uses
EGL import on the actual NVIDIA GPU, only advertised non-external modifiers,
and synchronous float readback before returning each sampled buffer. It received
1085 frames/30s, corrupt0, exit0, modifier0x0300000000e08014. Samples300/600/900
preserved all four source codes exactly:520/594/668/769, corresponding to
100.230/202.915/401.505/998.932nits after10-bit quantization. The first sample
included the consent dialog and is excluded from band qualification. The late
raw-image preview was opened: four gray bands and the black moving marker,
without the false colors seen when the shared-memory bytes were unpacked as10bit.
This is HDR **capture evidence only**, not encoding/network/TV/HDR game proof.
The production portal adapter remains SDR-only. Evidence lives under
~/umbra-flatpak/vega/gnome/hdr-dma-capture.log and portal-hdr-dma-0900.rgba32f.

## HDR custom-encoder component — 2026-10-12 14:43

Linux ABI2 adds explicit `AGHL_XBGR2101010_PQ` input. The new Linux-only kernel
unpacks little-endian RGB10, converts BT.2020/PQ to extended linear scRGB, then
uses existing aspect-fit and the unchanged shared Windows capture kernel. HDR
requires10bit and either explicit PQ or scRGB; PQ is rejected for SDR sessions.
ABI1 callers fail explicitly. Both depth libraries compiled in SDK25 with
warnings as errors. The installed Umbra package/adapter is still ABI1/SDR;
these new libraries ran only from the isolated `gpu-hdr-abi2` diagnostic directory.

The live portal DMA source fed this library synchronously and sent packets
through a FIFO to a live FFmpeg decoder. No saved image/video was encoder input.
Two75s runs completed: gray-only2990 frames, then gray plus saturated BT.2020
red/green/blue/white2981 frames. Each delivered2 IDRs (initial + requested at
submission120), corrupt0, exit0. FFmpeg identified Main10, limited
BT.2020 nonconstant-luminance / BT.2020 / SMPTE2084,1920x1088. Late decoded images
were opened: correct bands/channel ordering, moving marker, and centered
aspect-fit with thin black top/bottom bars. This is about40fps for this probe,
not60fps or a performance qualification.

Decoded gray-band Y codes511/574/638/725 and Cb/Cr514/514 reproduce the shared
Windows kernel's normalized16-bit/P010 scaling. Nominal luma-only PQ values are
102.21/205.94/410.09/1025.44nits, not exact source luminances; the chroma offset
also means those are not measured RGB luminances. Decoded colored-band RGB PQ
code estimates were red(674.20,21.34,18.69), green(49.07,668.67,34.38),
blue(27.43,13.22,672.55), white(673.69,668.64,674.62). Windows HDR parity and
color accuracy remain open; do not call this bit-exact HDR preservation.
Evidence: ~/umbra-flatpak/vega/gnome/{hdr-live,hdr-live-color}/. Manual negative
checks in tests/linux_abi_contract.c passed on the actual Flatpak GPU: reject
ABI1, BGRA input to HDR, PQ input to SDR, and undersized capture buffers.

The read-only `tests/coop/wayland_hdr_metadata.c` query also worked inside the
Flatpak. GNOME HDR DP-1 reported BT.2020/PQ, D65, target primaries matching2020,
target min0.005/max10000nits and reference203. SDR reported709/sRGB, min0.2/max80.
These are compositor-reported theoretical target values, not physical luminance
measurements of the headless output. The query checks ready/done and separately
reports primary and target color volumes. A production implementation still
must match the portal-selected output, track changes and feed client metadata.
No host integration, network HDR notification, TV, HDR game, mixed-host or AMD
result is implied by this component checkpoint. All owned harnesses stopped;
temporary HDR output changes reverted to SDR.

## GPU selection — 2026-10-12

ABI3 adds an optional exact OpenCL device name or `pci:dddd:bb:dd.f` selector to
aghl_config and aghl_probe_device. Existing aghl_probe remains an auto wrapper;
ABI1/2 session opens are rejected. Explicit unavailable or duplicate matches never
fall back. The host uses its existing Graphics Adapter setting, converting DRM
character-device identity through /sys/dev/char to a PCI address, and uses the
same selector for capability discovery and session opening. Restart after changes.

Auto preference uses CL_DEVICE_HOST_UNIFIED_MEMORY as a topology heuristic, not
a speed measurement. Multiple separate-memory GPUs remain ambiguous; set an
exact device. PCI matching requires per-device cl_khr_pci_bus_info; drivers
without it can still use unique names. Duplicate names require a unique PCI
identity; duplicate ICD identities fail explicitly. These policies do not qualify
Intel/AMD or a physical multi-GPU system.

SDK25 builds of both depths and selection-policy fixtures passed. In the actual
VEGA NVIDIA Flatpak sandbox, both depths probed/opened successfully by auto, exact
name and PCI, while unknown name/address were rejected. ABI1/2, wrong HDR pixel
formats and short buffers were rejected before frame encoding. Tests opened GPU
resources but encoded no synthetic frames. Live selected-device network check is
still required for this checkpoint. Evidence: gpu-linux/selection-sandbox-checks.log.
