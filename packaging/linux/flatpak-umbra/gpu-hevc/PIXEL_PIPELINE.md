# GPU residual and coefficient pipeline

**ESTABLISHED — implemented:** the development pipeline connects the following
GPU stages without downloading and re-uploading coefficients or syntax bins:

```text
source pixels + supplied prediction pixels
    → residual → integer DCT/DST → scalar quantization
    → coefficient-group syntax → parallel CABAC → compressed coefficient payload
                            ↘ inverse quantization/transform → reconstruction
```

**ESTABLISHED — limits:** this is not a complete HEVC picture encoder. Prediction
pixels and transform choices are test inputs; motion search, intra prediction,
mode decisions, CU/TU tree syntax, filters, rate control, capture interop and
complete NAL/session packaging remain absent. The output cannot be given to a
decoder as a complete picture. No Apollo or Aurora production path uses it.

## Arithmetic and syntax scope

**ESTABLISHED — source:** `kernels/residual.cl` adapts the pinned x265 integer
transforms and scalar quantization. It supports 4/8/16/32-sample square transforms,
4×4 DST, 8-/10-bit samples, QP 0–51 with bit-depth adjustment, I/P rounding,
inverse transforms and clipped reconstruction. Scaling lists, RDOQ, sign-data
hiding, transform bypass and transform skip are not implemented by this kernel.
Chroma QP mapping is a caller responsibility; the tests do not implement a
complete chroma plane pipeline. Transform size and bit depth select compiled
variants; mismatched runtime values are rejected.

**ESTABLISHED — syntax:** `kernels/coefficients.cl` adapts x265 coefficient
binarization: last position, significance groups/maps, magnitude flags, signs and
Rice/Exponential-Golomb remainders. It covers luma/chroma coefficient contexts,
diagonal scans at every size, horizontal/vertical luma scans at 4/8 and chroma
scans at 4. The tested contract disables sign hiding and transform-skip syntax.
Empty TUs emit no coefficient syntax; the surrounding CBF/tree syntax is absent.

**ESTABLISHED — parallel work:** a TU workgroup resolves its nonzero groups and
last coefficient. Each 4×4 coefficient group then generates syntax independently,
using the previous nonempty group's magnitude state. A counting pass plus GPU
prefix scan places output bins in the same order as x265. A writing pass emits
them. CABAC consumes that GPU buffer directly. The test host reads the total bin
count and error flag before dispatching CABAC; this synchronization is included
in the integrated measurement. Initial CABAC contexts are supplied by the test
oracle, not initialized by a new production session implementation.

## Reproduce

Use the exact x265 and Khronos header pins and the disposable oracle checkouts
described in [README.md](README.md). CPU x265 is an independent test oracle only.
Run these commands from the Aurora root, using new output directories.

```sh
bash experimental/gpu-hevc/tests/build_residual_oracle.sh X265 ORACLE8 8 OUT/residual_oracle8
bash experimental/gpu-hevc/tests/build_residual_oracle.sh X265 ORACLE10 10 OUT/residual_oracle10
mkdir OUT/fixtures8 OUT/fixtures10
OUT/residual_oracle8 OUT/fixtures8
OUT/residual_oracle10 OUT/fixtures10

bash experimental/gpu-hevc/tests/build_coeff_oracle.sh X265 ORACLE8 OUT/coeff_oracle
mkdir OUT/coeff-fixtures
AURORA_CABAC_TRACE="$(realpath OUT/coeff-fixtures)" OUT/coeff_oracle OUT/coeff-fixtures
```

Configure/build `experimental/gpu-hevc` as in the README. CMake now builds
`gpu_cabac_smoke`, `gpu_residual_smoke` and `gpu_coeff_smoke`. Select `NVIDIA` or
`Advanced` as the vendor substring. Windows executables require Windows paths.

```sh
gpu_residual_smoke experimental/gpu-hevc/kernels/residual.cl OUT/fixtures8 NVIDIA
gpu_residual_smoke experimental/gpu-hevc/kernels/residual.cl OUT/fixtures10 NVIDIA
gpu_coeff_smoke experimental/gpu-hevc/kernels/cabac_parallel.cl \
  experimental/gpu-hevc/kernels/coefficients.cl OUT/coeff-fixtures NVIDIA
```

For an integrated test, select one residual fixture and create a separate new
directory. The oracle reads its already-computed reference coefficients and
uses upstream x265 to produce the independent syntax/CABAC reference:

```sh
mkdir OUT/integrated-case
cp OUT/fixtures8/native-d8-n16.residual OUT/integrated-case/slice-0000.residual
AURORA_CABAC_TRACE="$(realpath OUT/integrated-case)" \
  OUT/coeff_oracle OUT/integrated-case OUT/integrated-case/slice-0000.residual
gpu_coeff_smoke experimental/gpu-hevc/kernels/cabac_parallel.cl \
  experimental/gpu-hevc/kernels/coefficients.cl OUT/integrated-case NVIDIA \
  experimental/gpu-hevc/kernels/residual.cl
```

**ESTABLISHED — independence:** in integrated mode the GPU starts from source
and prediction pixels. Reference coefficients and bins are used only in checks;
they are not uploaded as inputs to the GPU encoding stages. The generated bins
are compared to the oracle (kind/value/context index), followed by every CABAC
output byte and final context. Reconstructed pixels and quantized coefficients
are also checked. Separate residual tests check all six arithmetic outputs.

## What PASS and timings mean

**ESTABLISHED — test extent:** residual fixtures exercise all 52 requested QPs,
positive/negative extremes, zero residuals, alternating values, impulses and
random data, in both bit depths. Additional workloads each contain 4,177,920
luma samples, the same count as either candidate coded pane. They are packed
TU batches, not tests of horizontal/vertical display geometry. Fourteen syntax
fixtures cover 64 TUs each, including empty groups, sparse/dense coefficients,
scan modes, chroma/luma contexts and large Rice remainders.

**ESTABLISHED — timing:** five warm-ups precede eleven measured iterations.
Buffers and input pixels are allocated/uploaded beforehand. Standalone residual
GPU times cover arithmetic; its host times also include downloading coefficients
and reconstructed pixels for validation. Those large test readbacks are not
part of the connected GPU pipeline. Integrated times include residual arithmetic,
syntax metadata, both bin passes, prefix scans, the count/error readback, CABAC
and, for the host interval, compressed payload readback. `STAGE` is the first
measured trial's summed kernel durations, excluding gaps; aggregate device spans
include gaps. The harness uses reference-sized operation/output capacities plus
slack, not a production allocation policy.

**ESTABLISHED — interpretation:** native-sized noise fixtures are deliberately
dense stress inputs. Their compressed coefficient payload is 1.49–2.12 MB per
luma batch. They are not ordinary gameplay, a bitrate-controlled stream, a whole
YUV frame or capture-to-panel latency. All tests are offline component checks.
No game-contention, HDR presentation, live streaming or visual acceptance result
can be inferred from PASS.

**HYPOTHESIS — next integration:** prediction generation, mode/tree decisions,
chroma, filtering and complete headers can turn these components into a complete
picture encoder. Correctness and timing of that combined encoder remain to be
established before Apollo registration or a TV build.

## Separate inter-prediction primitive

**ESTABLISHED — implementation:** `kernels/prediction.cl` ports x265's unweighted,
single-reference interpolation. A reference plane and per-block origin/MV produce
packed prediction pixels. This is ordinary host encoder work, not Aurora's
composition or reference-header coordination. Luma uses quarter-sample vectors;
4:2:0 chroma uses the same vector units and eighth-chroma-sample filter phases.
The kernel preserves x265's internal signed precision between horizontal and
vertical filters. Reference edges are replicated. Input reference samples must
already fit the compiled bit depth; the caller supplies legal block origins and
motion vectors. There is no motion search, MV predictor/merge selection, weighted
prediction or intra prediction in this primitive.

**ESTABLISHED — checks:** `prediction_oracle.cpp` calls the original pinned x265
C primitives, including its intermediate signed filtering path. The GPU receives
reference pixels and vectors, not expected prediction pixels. Tests compare every
output pixel, reject an invalid vector before writing that block, and check an
output guard. The fixtures cover every fractional phase, positive/negative integer
displacements, replicated margins, six pixel patterns, 8/10 bit, luma block sizes
4/8/16/32 and chroma 4/8/16. Native-shaped reference fixtures are 3840×1088 and
1920×2176 luma, with half-width/height chroma. These are arithmetic checks, not
panel geometry or visual acceptance tests.

```sh
bash experimental/gpu-hevc/tests/build_prediction_oracle.sh X265 ORACLE8 8 OUT/prediction_oracle8
bash experimental/gpu-hevc/tests/build_prediction_oracle.sh X265 ORACLE10 10 OUT/prediction_oracle10
mkdir OUT/prediction8 OUT/prediction10
OUT/prediction_oracle8 OUT/prediction8
OUT/prediction_oracle10 OUT/prediction10
# CMake also builds gpu_prediction_smoke; select NVIDIA or Advanced.
gpu_prediction_smoke experimental/gpu-hevc/kernels/prediction.cl OUT/prediction8 NVIDIA
gpu_prediction_smoke experimental/gpu-hevc/kernels/prediction.cl OUT/prediction10 NVIDIA
```

**ESTABLISHED — measurement scope:** five warm-up runs plus eleven timed runs;
inputs are already uploaded. Device time is one interpolation kernel. Host time
includes downloading all output pixels for comparison. There is no capture,
motion search, residual coding, game contention or presentation in this timing.
The new primitive is tested separately, not yet connected to residual/CABAC.
