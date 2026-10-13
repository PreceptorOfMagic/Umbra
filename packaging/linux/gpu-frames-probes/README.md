# GPU-frame hand-off leg tests (co-op GPU HEVC encoder)

The co-op encoder (`gpu-hevc`, OpenCL) takes captured frames without a host copy
(`src/platform/linux/gpu-hevc-frames.{h,cpp}`). One GL draw packs the captured DMA-BUF into a
GL_R32UI texture; the hand-off to OpenCL depends on what the GPU's OpenCL can import:

| OpenCL | Can import | Hand-off |
|---|---|---|
| NVIDIA | opaque fd (`cl_khr_external_memory_opaque_fd`); no DMA-BUF, EGL sharing refused (-59) | CUDA copies the texture into a VMM export -> `AGHL_FRAME_EXTERNAL` |
| Mesa rusticl (AMD, Intel in the Flatpak runtime) | `cl_khr_gl_sharing`; no external memory (Mesa 26.2.2) | shared EGL context -> `AGHL_FRAME_GL_TEXTURE` |
| anything else, or a failed step | - | read the packed texture back (`readback`) |

Probes (each prints PASS/FAIL; build lines in the file headers):

- `bridge-probe.c`: DMA-BUF -> CUDA routes. VEGA, RTX 2060 SUPER, driver 595.99.02, 2026-10-13:
  CUDA registers neither a DMA-BUF EGLImage nor a texture bound to one (CUDA_ERROR_INVALID_VALUE);
  the GL draw into an owned R32UI texture + CUDA copy is bit-exact for 8- and 10-bit, 0.46 ms at
  1920x2176, 0.62 ms at 3840x2160.
- `glshare-probe.c`: the rusticl sequence (GL-shared context, `clCreateFromGLTexture` ->
  CL_R/CL_UNSIGNED_INT32, acquire, copy image -> buffer, release). Bit-exact (4,177,920 words)
  on rusticl over zink/lavapipe, on Ubuntu 26.04 Mesa 26.0.8 and inside the Flatpak runtime
  (Mesa 26.2.2). Rusticl on llvmpipe does not offer GL sharing.
- `glshare-encode-probe.c`: the library end to end (`aghl_gl_sharing`, GL texture frames, the
  `AGHL_EXTERNAL_UNAVAILABLE` resubmit). With rusticl on zink over the RTX 2060 SUPER (host Mesa
  26.0.8): the encoder's kernels run on rusticl and decode within 2/255 of the source pattern;
  GL sharing is set up (`aghl_gl_sharing=1`); zink-on-NVIDIA cannot export the texture (OpenCL -6),
  the library returns -2 with the session intact, and resubmitted frames are byte-identical to a
  host-pixel run. On lavapipe the encoder's kernels crash in the JIT (host pixels too).

Not measured: an AMD GPU (radeonsi exporting the texture to rusticl). Environment used for rusticl
on zink over NVIDIA (host only; NVIDIA's Vulkan driver does not start inside the sandbox once the
EGL vendor list is forced): `__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json:.../10_nvidia.json
MESA_LOADER_DRIVER_OVERRIDE=zink RUSTICL_ENABLE=zink:0 RUSTICL_DEVICE_TYPE=gpu`.
