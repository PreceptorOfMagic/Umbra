# Experimental GPU HEVC picture pipeline

**ESTABLISHED — source:** `src/picture_encoder.h` connects GPU prediction,
residual/transform/quantization, reconstruction, syntax and parallel CABAC. It
retains reconstructed pictures on the GPU. `src/aurora_gpu_hevc.cpp` imports a
D3D11 texture, converts colour on the GPU, submits a picture and returns Annex-B
bytes. `src/x265_headers.h` uses pinned x265
`b81f650e21e8aacbe6a9ad04ce14aefc05b932c0` for configuration and slice headers,
not `x265_encoder_encode`. Capture pixels are not downloaded for production
encoding. `agh_encode` is synchronous; the caller serializes calls and owns
capture scheduling. There is no encoder frame queue or added relay.

## Contract and limits

**Source contract:** CTB32, minimum coding block16, one slice and one reference;
no B pictures, temporal MVP, weighted prediction, WPP or SAO. The rate-controlled
profile enables GPU deblocking with filtering across slices disabled. It filters
only interior pane edges, after current-picture reconstruction and before reference
commit. The deblock QP scan includes inherited QPs for zero-residual CUs.

Pictures use unsplit luma CU/TU32 and chroma TU16. Full-I pictures use DC prediction.
P pictures can choose DC intra in one position of a rotating 2×2 CU grid. The other
three positions reconstruct first as inter. Eligible CUs then choose intra/inter
using reconstructed top/left references and reconstruct once; none of those
references belongs to an eligible neighbor. This preserves GPU parallelism without
adding rows to a frame queue. Intra is unconstrained, and quantization rounding
continues to depend on slice type. AMVP and deblocking account for intra CUs.

Motion search retains original luma on the GPU for sparse wider candidate proposals.
Complete-block ranking, diamond search and quarter-pixel refinement use reconstructed
references, as does actual HEVC prediction. Search stays within ±32 pixels. A fitted quarter-pixel proposal is
accepted only when full-block SAD plus motion cost improves with normative HEVC
interpolation. The complete luma/chroma filter footprint stays within its pane;
vector-origin clipping alone would permit cross-pane reference samples after splicing.

The feedback controller uses QP18–51 with quarter-step internal quality. A balanced,
rotating spatial mix of neighboring integer CU QPs implements fractional steps.
The former activity mask that suppressed dense fine texture is removed. Both hosts
use one quantization group per CTU; logged QP is the slice/base QP. Quality pressure
above QP51 is absent because it cannot change this profile's coefficients.

Actual Annex-B bytes are charged against negotiated bitrate and this host's elapsed
submission time. Bounded positive and negative rate history adjusts the next target;
it is not an AU cap. The controller learns the local bit-size/quantization slope
from attempts already made on the same picture. It never sleeps, synchronizes
hosts, drops captures or adds a picture queue. Apollo's common packetizer remains
responsible for packetization. Up to three encode attempts can occur, committing
reconstruction once. Retries reuse motion/inter prediction, then rebuild current
neighbors and intra decisions at the new QP. No extra trial is added for learning.

v11 uses the remaining third attempt to recover a bracketed pass-2 undershoot
below 80% of target by interpolating log(bytes) in quarter-QP units. If pass 2
reaches QP51, it retains that measured result rather than trusting the wide bracket.
For pass-2 overflow with two ordered measurements, it extrapolates log(bytes)
against quantizer step `2^(quarterQuality/24)`, falling back to the existing
rate increase when the model inputs are invalid. The three-attempt limit is unchanged.

The NV12/P010 conversion follows Apollo's normalized legacy color vectors. P010
rounds to 16-bit UNORM before retaining the high 10 bits, matching the native shader
path rather than directly multiplying studio-range code values by four.

General partition/mode search and full x265 ABR/VBV remain unfinished. Quality,
component timing and current qualification evidence are separated in
[the parity report](PARITY_2026_09_16.md).
The shared 120-fps VUI value is syntax, not a pacing loop; its interaction with TV
presentation must be checked live.

**ESTABLISHED — source:** both hosts receive the same encoder contract. D3D11
interop selects the OpenCL GPU associated with Apollo's capture adapter, using
KHR sharing on AMD and NV sharing on NVIDIA. This avoids selecting an unrelated
GPU by vendor name. The API checks exact capture dimensions, colour format,
monotonic frame identity and context state; failures refuse the session. HDR
requires scRGB input and emits limited BT.2020/PQ at 10 bits. SDR uses limited
BT.709. ABI 3 is experimental, not a stable external SDK.

**ESTABLISHED — measured scope:** RTX 4070 driver 610.74 and RX Vega gfx900 driver
3444.0 (PAL,HSAIL) passed the D3D11 texture, host-DLL and shared-texture adapter
smokes. That includes native row panes (3840×1088), column panes (1920×2176),
8-bit SDR and 10-bit HDR. **HYPOTHESIS:** other old and new GPUs providing the
required capabilities will work; GPU age alone is neither acceptance nor
rejection. Other GPU families/drivers and performance while gaming are untested.

## Build and checks

Use Khronos OpenCL-Headers commit `386ca390b2f52efeb3e1a55a500690eb8013f60e`.
For Windows host DLLs, build clean static x265 separately in 8- and 10-bit modes
using MSVC and `/MT`; configure this directory with `BUILD_HOST_DLLS=ON`,
`OPENCL_HEADERS`, `X265_SOURCE`, `X265_BUILD_8`, and `X265_BUILD_10`. Build Release.
Place `aurora_gpu_hevc8.dll`, `aurora_gpu_hevc10.dll` and `kernels/` in Apollo's
adjacent `gpu-hevc/` directory. Apollo integration is in the separate Apollo
checkout, especially `src/platform/windows/gpu_hevc_device.inc`.

- `gpu_texture_picture_smoke`: renders changing D3D textures, checks colour
  conversion against an independent CPU observer (outside timing), writes the
  stream and GPU reconstruction.
- `check_picture_decode.py`: independently decodes with FFmpeg and compares
  every pixel against reconstruction; optionally runs the actual C splicer.
- `gpu_rate_picture_smoke8/10`: noisy changing native-size textures, variable
  slice and CTU QP, explicit IDRs and bounded retry reconstruction. Independently compare
  both streams and composites using `check_picture_decode.py`.
- `gpu_library_smoke`: exercises two sessions, explicit IDRs, non-contiguous
  frame identities and failure handling; compares canonical bytes to texture
  fixtures. Long fixtures cross the native POC wrap.
- `apollo_adapter_smoke`: compile with `APOLLO_ADAPTER_SOURCE` pointing at the
  actual adapter. Exercises separate capture/encode D3D devices, NT sharing,
  keyed mutexes, producer recycling and reused image identities. It does not
  replace a live Apollo capture test.
- `check_core_cadence.py` with `core_cadence_smoke`: exercises the unchanged C
  splicer, POC rewriting and skip generator with unequal arrival sequences,
  isolated IDRs, held panes and composite POC wrap, both layouts/primary roles.
  Independent decoding must reproduce the identified source panes exactly.

**ESTABLISHED — test boundary:** these offscreen/offline checks establish encoder
and splice correctness for their inputs. They are not a live streaming latency
verdict. Real host→TV sessions and advancing panel pictures are required before
asking the user to test latency. No fixture is replayed on the TV.

## Experimental selection

**ESTABLISHED — source:** the client must be built with
`AURORA_GPU_HEVC_EXPERIMENTAL=ON` (now the default). There is no activation
marker. Existing GPU reports and saved mappings select the shared encoder for
NVIDIA+AMD and AMD+AMD; NVIDIA×NVIDIA retains NVENC. This automatic selection
supersedes an old saved H.264 preference for these pairs. The co-op dialog shows
“Beta HEVC” or “HEVC core”; settings explain automatic selection. Unknown/untested
pairs and builds with the option disabled retain the legacy policy. Launch/resume requires Apollo's `auroraGpuHevc=1` acknowledgement;
missing support fails instead of silently falling back to incompatible HEVC.
Use the normal app ID on this rig; the separate beta ID has an unresolved decoder
loading problem documented in passovers 10–12.

**ESTABLISHED — source:** custom sessions bypass Apollo's legacy AVC
`coop_pad_rows` display shrink. Their capture and encode dimensions must agree.
No automatic rate cap or rate matching is introduced. The rounded pane sizes
remain subject to the user's visual half-panel acceptance test.

**ESTABLISHED — source and VEGA resume smoke:** before resuming a paused custom
session, Apollo verifies the requested native capture geometry. If Windows rejects
the new mode, it recreates that paused virtual display with the same identity and
verifies its dimensions before capture. This is restricted to custom sessions with
no active streams. See [passover 22](../../docs/passover/22-from-astra-gpu-hevc-live-integration.md)
for the UI and display integration; [passover 23](../../docs/passover/23-from-astra-beta-hevc-quality.md)
records the current quality build, installed host versions and outstanding acceptance.
