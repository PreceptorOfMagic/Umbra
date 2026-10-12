# Linux host library — development status, 2026-10-12

The Linux OpenCL adapter shares the Windows v12b picture encoder, rate controller,
HEVC headers and eight existing kernels. Before this port, all nine source files
matched the v12 build source and all eight kernel hashes matched the release's
BUILD-PROVENANCE.json. Linux has a separate ABI (AGHL version 1); Windows ABI 3 is
unchanged. The shared RPS reset now uses its constructor, which initializes every
field, instead of memset, to compile with GCC's class-memaccess warning enabled.
A Windows rebuild/regression check remains due.

The Linux adapter uploads CPU capture pixels to a GPU image. Conversion, aspect
fitting and picture encoding run through OpenCL. There is no CPU picture-encoder
fallback. Exactly one usable OpenCL GPU is currently required; selecting between
multiple GPUs is unfinished. The output libraries export only the four AGHL
entry points; their private x265 symbols cannot interpose with each other or the
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
