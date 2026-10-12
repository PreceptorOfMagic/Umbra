// SPDX-License-Identifier: GPL-2.0-or-later
// Independent fixture generator using pinned x265 C primitives, never streaming.
#include "common.h"
#include "primitives.h"
#include "scalinglist.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace X265_NS;
static void write(FILE* f, const void* p, size_t n) {
    if (fwrite(p, 1, n, f) != n) { fprintf(stderr, "write failed\n"); exit(1); }
}
static uint32_t random32(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static void make(const char* dir, unsigned log2, unsigned count, bool native) {
    unsigned n = 1u << log2, area = n * n;
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s-d%d-n%u.residual", dir, native ? "native" : "edges", X265_DEPTH, n);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    write(f, "AXRES001", 8);
    uint32_t header[] = { X265_DEPTH, log2, count, native ? 1u : 0u };
    write(f, header, sizeof(header));
    alignas(32) pixel input[1024], prediction[1024], reconstructed[1024];
    alignas(32) int16_t residual[1024], transformed[1024], quantized[1024], dequantized[1024], inverse[1024];
    alignas(32) int32_t scales[1024], delta[1024];
    uint16_t expanded[1024];
    unsigned seed = 0xc347d215u;
    const unsigned max = (1u << X265_DEPTH) - 1;
    for (unsigned block = 0; block < count; block++) {
        unsigned qp = native ? 28 : block % 52;
        unsigned intra = native ? 0 : (block / 52) % 2;
        unsigned dst = log2 == 2 && !native && ((block / 104) % 2);
        uint32_t meta[] = {qp, intra, dst, 0};
        write(f, meta, sizeof(meta));
        for (unsigned i = 0; i < area; i++) {
            unsigned a = random32(seed) & max, b = random32(seed) & max;
            switch ((block / 208) % 6) {
            case 0: b = a; break;
            case 1: a = max; b = 0; break;
            case 2: a = 0; b = max; break;
            case 3: a = (i & 1) ? max : 0; b = max - a; break;
            case 4: b = max / 2; a = b + (i == area / 2 ? 1 : 0); break;
            default: break;
            }
            // Native workload: varying prediction residuals, not all zero blocks.
            if (native) { a = random32(seed) & max; b = (a + (random32(seed) % 65) + max - 32) & max; }
            input[i] = pixel(a); prediction[i] = pixel(b);
        }
        primitives.cu[log2 - 2].sub_ps(residual, n, input, prediction, n, n);
        (dst ? primitives.dst4x4 : primitives.cu[log2 - 2].dct)(residual, transformed, n);
        int effective = int(qp) + 6 * (X265_DEPTH - 8), per = effective / 6, rem = effective % 6;
        int transformShift = 15 - X265_DEPTH - int(log2);
        int qbits = 14 + per + transformShift;
        int add = (intra ? 171 : 85) << (qbits - 9);
        for (unsigned i = 0; i < area; i++) scales[i] = ScalingList::s_quantScales[rem];
        primitives.quant(transformed, scales, delta, quantized, qbits, add, area);
        primitives.dequant_normal(quantized, dequantized, area, ScalingList::s_invQuantScales[rem] << per, 6 - transformShift);
        (dst ? primitives.idst4x4 : primitives.cu[log2 - 2].idct)(dequantized, inverse, n);
        primitives.cu[log2 - 2].add_ps[NONALIGNED](reconstructed, n, prediction, inverse, n, n);
        for (unsigned i = 0; i < area; i++) expanded[i] = input[i];
        write(f, expanded, area * 2);
        for (unsigned i = 0; i < area; i++) expanded[i] = prediction[i];
        write(f, expanded, area * 2);
        write(f, residual, area * 2); write(f, transformed, area * 2);
        write(f, quantized, area * 2); write(f, dequantized, area * 2); write(f, inverse, area * 2);
        for (unsigned i = 0; i < area; i++) expanded[i] = reconstructed[i];
        write(f, expanded, area * 2);
    }
    if (fclose(f)) { perror("close"); exit(1); }
    printf("fixture=%s blocks=%u samples=%u\n", path, count, count * area);
}
int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "Usage: residual_oracle existing-output-directory\n"); return 2; }
    setupCPrimitives(primitives);
    for (unsigned lg = 2; lg <= 5; lg++) make(argv[1], lg, 6 * 208, false);
    // Each has 3840*1088 == 1920*2176 luma samples; shape is not a geometry test.
    make(argv[1], 4, 3840u * 1088u / 256u, true);
    make(argv[1], 5, 3840u * 1088u / 1024u, true);
    return 0;
}
