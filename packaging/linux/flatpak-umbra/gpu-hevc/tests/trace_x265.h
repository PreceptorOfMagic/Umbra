// SPDX-License-Identifier: GPL-2.0-or-later
// Included into the disposable x265 entropy.cpp by instrument_x265.py.
// Independent oracle: these hooks observe upstream output; they do not replace it.
#include <array>
#include <map>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
struct Trace {
    std::array<uint8_t, 160> initial;
    std::vector<std::array<uint32_t, 4> > ops;
};
std::map<X265_NS::Entropy*, Trace> traces;
std::mutex traceMutex;
unsigned traceId = 0;

void traceFail(const char* why) {
    fprintf(stderr, "CABAC oracle INVALID: %s\n", why);
    abort();
}
void traceOp(X265_NS::Entropy* e, uint32_t kind, uint32_t value, uint32_t arg, uint32_t state) {
    const char* dir = getenv("AURORA_CABAC_TRACE");
    if (!dir || !e->m_bitIf) return; // Exclude RDO estimates.
    std::lock_guard<std::mutex> lock(traceMutex);
    auto found = traces.find(e);
    if (found == traces.end()) {
        if (e->m_low || e->m_range != 510 || e->m_bitsLeft != -12 ||
            e->m_numBufferedBytes || e->m_bitIf->getNumberOfWrittenBits())
            traceFail("not a fresh byte-aligned single CABAC stream");
        Trace t;
        t.initial.fill(0);
        memcpy(t.initial.data(), e->m_contextState, MAX_OFF_CTX_MOD);
        found = traces.emplace(e, t).first;
    }
    if (kind == 0 && arg >= MAX_OFF_CTX_MOD) traceFail("context outside context array");
    found->second.ops.push_back({kind, value, arg, state});
}
void traceWrite(FILE* f, const void* data, size_t size) {
    if (fwrite(data, 1, size, f) != size) traceFail("write failed");
}
void traceEnd(X265_NS::Entropy* e) {
    const char* dir = getenv("AURORA_CABAC_TRACE");
    if (!dir) return;
    std::lock_guard<std::mutex> lock(traceMutex);
    auto found = traces.find(e);
    if (found == traces.end()) traceFail("finish without operations");
    X265_NS::Bitstream* bits = dynamic_cast<X265_NS::Bitstream*>(e->m_bitIf);
    if (!bits || bits->getNumberOfWrittenBits() % 8) traceFail("unfinished stream");
    char path[4096];
    snprintf(path, sizeof(path), "%s/slice-%04u.cabac", dir, traceId++);
    FILE* f = fopen(path, "wb");
    if (!f) traceFail("cannot open trace file");
    uint32_t counts[2] = {uint32_t(found->second.ops.size()), bits->getNumberOfWrittenBytes()};
    std::array<uint8_t, 160> finalState = {};
    memcpy(finalState.data(), e->m_contextState, MAX_OFF_CTX_MOD);
    traceWrite(f, "ACABAC01", 8);
    traceWrite(f, counts, sizeof(counts)); // little-endian x86 oracle only
    traceWrite(f, found->second.initial.data(), 160);
    traceWrite(f, finalState.data(), 160);
    traceWrite(f, found->second.ops.data(), found->second.ops.size() * 16);
    traceWrite(f, bits->getFIFO(), counts[1]);
    if (fclose(f)) traceFail("close failed");
    fprintf(stderr, "CABAC_TRACE %s ops=%u bytes=%u\n", path, counts[0], counts[1]);
    traces.erase(found);
}
}
