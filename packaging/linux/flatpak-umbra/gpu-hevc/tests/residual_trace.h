// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "opencl_test_runtime.h"

struct ResidualTrace {
    uint32_t depth, lg, blocks;
    std::vector<uint32_t> parameters;
    std::array<std::vector<uint16_t>, 8> planes;
    explicit ResidualTrace(const std::filesystem::path& path) {
        auto file = readFile(path);
        if (file.size() < 24 || memcmp(file.data(), "AXRES001", 8)) throw std::runtime_error("Invalid residual header");
        depth = le32(file.data()+8); lg = le32(file.data()+12); blocks = le32(file.data()+16);
        if ((depth != 8 && depth != 10) || lg < 2 || lg > 5 || !blocks || blocks > 1000000)
            throw std::runtime_error("Invalid residual dimensions");
        size_t area = size_t(1) << (2*lg), total = area * blocks;
        if (file.size() != 24 + size_t(blocks)*(16+16*area)) throw std::runtime_error("Residual size mismatch");
        parameters.resize(size_t(blocks)*4);
        for (auto& p : planes) p.resize(total);
        size_t at = 24;
        for (uint32_t b = 0; b < blocks; b++) {
            for (unsigned k = 0; k < 4; k++, at += 4) parameters[size_t(b)*4+k] = le32(file.data()+at);
            for (auto& p : planes) for (size_t i = 0; i < area; i++, at += 2)
                p[size_t(b)*area+i] = uint16_t(file[at] | unsigned(file[at+1]) << 8);
        }
    }
};
