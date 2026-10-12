# x265-derived GPU HEVC component development build

**ESTABLISHED:** this directory now includes a complete experimental GPU I/P-picture
encoder, D3D11 capture interop, 8-bit and 10-bit host DLLs, and Apollo integration.
Aurora continues to do composition with its existing HEVC splicer. CPU x265 supplies
headers and motion-cost tables; production picture encoding runs on the GPU.
No CPU encoding fallback is implemented.

This remains a restricted development profile; broad visual/performance parity
with NVENC is not established. It uses unsplit 32×32 coding units, DC intra
prediction, integer motion search with verified quarter-pixel refinement, one
reference, per-host bitrate feedback, fractional per-CU quality steps and GPU
deblocking. SAO and general partition/mode search are absent. There is no
beta-only access-unit size ceiling. Independent decoding and mixed-GPU composition
have passed on RTX 4070 and RX Vega; physical TV latency and game-load acceptance
remain separate measurements. Final guarded v11 is installed on both test hosts.
v10 had the latest TV test; v11 has offscreen qualification and awaits live acceptance.
Current hashes, results and rollback are in [the Claude handover](../../docs/passover/24-from-astra-beta-v11-claude-handover.md).
See [the picture pipeline guide](PICTURE_PIPELINE.md) for integration, limits and
reproduction; the earlier component work remains documented in
[PIXEL_PIPELINE.md](PIXEL_PIPELINE.md) and [ALGORITHM.md](ALGORITHM.md).

## Source and scope

The OpenCL kernel adapts arithmetic coding, bypass coding, termination, carry
handling and context-transition tables from x265 commit
`b81f650e21e8aacbe6a9ad04ce14aefc05b932c0`:

- `source/encoder/entropy.cpp`, especially `start`, `finish`, `encodeBin`,
  `encodeBinsEP`, `encodeBinTrm`, `writeOut`, and `g_nextState`.
- `source/common/constants.cpp`, `g_lpsTable`.
- `source/common/bitstream.cpp`, final byte-alignment semantics.

Upstream: <https://bitbucket.org/multicoreware/x265_git/>

The x265-derived components are GPL-2.0-or-later; attribution is retained and the
license text is in [COPYING](COPYING). Apollo-derived colour conversion, the host
adapter and DLL interface are GPL-3.0-only; see [COPYING-GPL-3](COPYING-GPL-3).
The combined DLL is GPL-3.0. This records source licensing, not a patent-license
conclusion; see passover 16 for the earlier licensing analysis.

**ESTABLISHED — original backend (`cabac.cl`):** each slice remains one dependent CABAC chain. A 64-work-item
group stages operations and probability tables; one lane codes the chain.
Dispatches process at most 65,536 operation records, saving the exact arithmetic
and context state on the GPU. Dispatch boundaries do not create extra slices,
WPP entry points, or frame-rate limits. This backend takes already-binarized
syntax. The newer residual and coefficient-syntax stages are described in
[PIXEL_PIPELINE.md](PIXEL_PIPELINE.md); the picture encoder now connects prediction, NAL packaging and Apollo capture.
General mode/partition search remains absent; the picture wrapper now provides
experimental bitrate feedback with bounded retries.

**ESTABLISHED — parallel backend (`cabac_parallel.cl`):** stable context grouping,
finite-state function scans, range resolution and parallel carry assembly produce
the same single-slice bytes without serially walking every operation. All those
passes execute on the GPU. It does not consume the oracle's expected context
states when producing timed output. The derivation, supported domain and current
costs are described in [ALGORITHM.md](ALGORITHM.md). It needs the whole slice's
binarized operations; streaming partial syntax into it is not implemented.

## Reproduce CABAC tests

Use disposable directories outside the Aurora/Apollo production builds. The
oracle is **CPU x265 for test comparison only**, never a host encoder substitute.
Both scripts fail on missing/mismatched input; do not run Python with `-O`.

1. Clone x265, check out the exact commit above, and run
   `python3 experimental/gpu-hevc/tests/instrument_x265.py /path/to/x265` once on
   that clean checkout. The script checks the pin, instruments emitted operations
   and bytes, and enables C++11 for the trace observer.
2. Build it with CMake from `x265/source`, using `ENABLE_ASSEMBLY=OFF`,
   `ENABLE_SHARED=OFF`, and `CMAKE_BUILD_TYPE=Release`. Use a separate build
   with `HIGH_BIT_DEPTH=ON` for the 10-bit oracle.
3. Run `python3 experimental/gpu-hevc/tests/make_traces.py /path/to/oracle/x265
   /new/output/directory --depth 8`, then the 10-bit executable with `--depth 10`
   and a different new directory. `--small` makes a quick 256×128 smoke fixture.
   The regular fixtures are three frames each at 3840×1088 and 1920×2176,
   including intra and predicted frames with temporal noise. Emitted SPS/PPS and
   slice fields are checked with FFmpeg, rather than inferred from CLI options.
4. Obtain Khronos OpenCL-Headers at commit
   `386ca390b2f52efeb3e1a55a500690eb8013f60e` from
   <https://github.com/KhronosGroup/OpenCL-Headers>.
5. Configure this directory with
   `cmake -S experimental/gpu-hevc -B /path/to/smoke-build
   -DOPENCL_HEADERS=/path/to/OpenCL-Headers`, then `cmake --build /path/to/smoke-build
   --config Release`. Windows needs a C++17 MSVC toolchain; Linux needs C++17 and
   `dlopen`. Both builds use warnings as errors. The OpenCL runtime is loaded
   dynamically; the Windows build uses the system directory.
6. Run `gpu_cabac_smoke[.exe] /path/to/cabac.cl /path/to/traces NVIDIA` on NVIDIA,
   or replace the vendor substring with `Advanced` for AMD. No arguments prints
   an inventory only. No matching GPU or no traces is a failure.
   For the parallel backend use `gpu_cabac_smoke[.exe] /path/to/cabac_parallel.cl
   /path/to/traces NVIDIA --parallel` (or vendor substring `Advanced` on AMD).

For arithmetic edge cases, build the 8-bit oracle library above with the current
instrumentation, then run `bash experimental/gpu-hevc/tests/build_vectors.sh
/path/to/x265 /path/to/oracle8 /path/to/cabac_vectors`. Create a fresh directory,
set `AURORA_CABAC_TRACE` to its absolute path and execute `cabac_vectors`. Feed the
resulting 69 `.cabac` files to the same GPU harness. These exercise empty payloads,
full-width bypass values, repeated MPS/LPS bins, random contexts, long carry cases,
and boundaries around 32, 256, 1024 and 65536 operations.

**ESTABLISHED — test meanings:** `PASS` means identical CABAC bytes and final
contexts, successful per-operation context checking on the first trial, and
working output-capacity/missing-termination guards. It does **not** mean that
latency, game performance, full HEVC encoding, HDR, or TV presentation passed.
The timing loop uses five warm-up runs and eleven measured runs. The operations
are uploaded before timing; per-operation oracle comparison is disabled for
timed runs, but output bytes and final contexts are checked after every run.

`device_span_ms` spans the first GPU dispatch's start through the last dispatch's
end, including device-side gaps. `submit_read_ms` spans CPU submission through
status and payload readback. Neither includes capture, earlier encode stages,
operation upload, a network hop, decoding, presentation, or game contention.
All parallel preprocessing, scratch clearing and GPU scans are included. The
scratch buffers are allocated before timing. `STAGE` reports the first measured
trial's summed device event durations by kernel; it excludes gaps between kernels.
The test sizes output capacity from the reference payload plus 64 bytes;
production allocation and buffer reuse remain unqualified.
`max_dispatch_ms` is measured, not a promised scheduling bound. These are
**offline component tests**, not a live streaming latency verdict.

## Trace ABI

Test-only little-endian format: `ACABAC01` (8 bytes), uint32 operation count,
uint32 reference-byte count, 160 initial context bytes, 160 final context bytes,
then `count` uint32[4] records and the reference payload. Records contain
`kind, value, context-index-or-bypass-bit-count, expected-pre-context`.
Kinds are context bin (0), bypass bins (1), and terminating bin (2).
The last operation must terminate the slice. The file contains the CABAC
substream including byte alignment, before NAL emulation-prevention insertion.

**ESTABLISHED:** the picture encoder supplies complete restricted CU/TU syntax
to this backend. Component smoke results remain separate from live qualification.
