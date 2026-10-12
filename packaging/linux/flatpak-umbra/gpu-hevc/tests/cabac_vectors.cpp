// SPDX-License-Identifier: GPL-2.0-or-later
// Independent unit vectors emitted by the pinned, instrumented x265 implementation.
// This executable is a CPU test oracle only; it is never a production encoder.
#include "common.h"
#include "entropy.h"
#include <cstdio>
#include <cstring>

namespace X265_NS {
struct AuroraCabacOracle {
    static uint32_t random(uint32_t& state) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state;
    }
    static void run(unsigned id, unsigned mode, unsigned count) {
        Entropy coder;
        Bitstream bits;
        uint32_t seed = 0x74a52931u + id;
        memset(coder.m_contextState, 0, sizeof(coder.m_contextState));
        for (unsigned c = 0; c < MAX_OFF_CTX_MOD; c++)
            coder.m_contextState[c] = uint8_t(mode == 3 ? 124 : mode == 4 ? 125 : random(seed) % 126);
        coder.setBitstream(&bits);
        coder.start();
        for (unsigned i = 0; i < count; i++) {
            unsigned c = random(seed) % MAX_OFF_CTX_MOD;
            if (mode == 1) coder.encodeBinsEP(0xffffffffu, 32);
            else if (mode == 2) coder.encodeBinsEP(i & 1 ? 0xffffffffu : 0, 32);
            else if (mode == 3) coder.encodeBin(coder.m_contextState[69] & 1, coder.m_contextState[69]);
            else if (mode == 4) coder.encodeBin((coder.m_contextState[69] ^ 1) & 1, coder.m_contextState[69]);
            else {
                unsigned kind = random(seed) % 5;
                if (kind < 3) coder.encodeBin(random(seed) & 1, coder.m_contextState[c]);
                else if (kind == 3) {
                    unsigned n = random(seed) % 33;
                    uint32_t value = random(seed);
                    if (n < 32) value &= (1u << n) - 1;
                    coder.encodeBinsEP(value, int(n));
                } else coder.encodeBinTrm(0);
            }
        }
        coder.finishSlice();
        printf("vector=%u mode=%u input_operations=%u output_bytes=%u\n", id, mode, count, bits.getNumberOfWrittenBytes());
    }
};
}
int main() {
    unsigned id = 0;
    for (unsigned mode = 0; mode < 6; mode++)
        for (unsigned n : {0u, 1u, 31u, 32u, 33u, 255u, 256u, 257u, 1023u, 1024u, 1025u})
            X265_NS::AuroraCabacOracle::run(id++, mode, n);
    for (unsigned n : {65535u, 65536u, 65537u}) X265_NS::AuroraCabacOracle::run(id++, 0, n);
    return 0;
}
