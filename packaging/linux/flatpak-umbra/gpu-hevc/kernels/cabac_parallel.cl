/* SPDX-License-Identifier: GPL-2.0-or-later
 * CABAC arithmetic coding port from x265 b81f650e21e8aacbe6a9ad04ce14aefc05b932c0.
 * Copyright (C) 2013-2020 MulticoreWare, Inc.
 * Original authors: Steve Borho, Min Chen (entropy.cpp), x265 contributors.
 * Aurora OpenCL adaptation, 2026. See COPYING and README.md.
 * Parallel finite-state scans and carry assembly; not a complete encoder.
 */
__constant uchar g_lpsTable[64][4] =
{
    { 128, 176, 208, 240 },
    { 128, 167, 197, 227 },
    { 128, 158, 187, 216 },
    { 123, 150, 178, 205 },
    { 116, 142, 169, 195 },
    { 111, 135, 160, 185 },
    { 105, 128, 152, 175 },
    { 100, 122, 144, 166 },
    {  95, 116, 137, 158 },
    {  90, 110, 130, 150 },
    {  85, 104, 123, 142 },
    {  81,  99, 117, 135 },
    {  77,  94, 111, 128 },
    {  73,  89, 105, 122 },
    {  69,  85, 100, 116 },
    {  66,  80,  95, 110 },
    {  62,  76,  90, 104 },
    {  59,  72,  86,  99 },
    {  56,  69,  81,  94 },
    {  53,  65,  77,  89 },
    {  51,  62,  73,  85 },
    {  48,  59,  69,  80 },
    {  46,  56,  66,  76 },
    {  43,  53,  63,  72 },
    {  41,  50,  59,  69 },
    {  39,  48,  56,  65 },
    {  37,  45,  54,  62 },
    {  35,  43,  51,  59 },
    {  33,  41,  48,  56 },
    {  32,  39,  46,  53 },
    {  30,  37,  43,  50 },
    {  29,  35,  41,  48 },
    {  27,  33,  39,  45 },
    {  26,  31,  37,  43 },
    {  24,  30,  35,  41 },
    {  23,  28,  33,  39 },
    {  22,  27,  32,  37 },
    {  21,  26,  30,  35 },
    {  20,  24,  29,  33 },
    {  19,  23,  27,  31 },
    {  18,  22,  26,  30 },
    {  17,  21,  25,  28 },
    {  16,  20,  23,  27 },
    {  15,  19,  22,  25 },
    {  14,  18,  21,  24 },
    {  14,  17,  20,  23 },
    {  13,  16,  19,  22 },
    {  12,  15,  18,  21 },
    {  12,  14,  17,  20 },
    {  11,  14,  16,  19 },
    {  11,  13,  15,  18 },
    {  10,  12,  15,  17 },
    {  10,  12,  14,  16 },
    {   9,  11,  13,  15 },
    {   9,  11,  12,  14 },
    {   8,  10,  12,  14 },
    {   8,   9,  11,  13 },
    {   7,   9,  11,  12 },
    {   7,   9,  10,  12 },
    {   7,   8,  10,  11 },
    {   6,   8,   9,  11 },
    {   6,   7,   9,  10 },
    {   6,   7,   8,   9 },
    {   2,   2,   2,   2 }
};

__constant uchar g_nextState[128][2] =
{
    { 2, 1 }, { 0, 3 }, { 4, 0 }, { 1, 5 }, { 6, 2 }, { 3, 7 }, { 8, 4 }, { 5, 9 },
    { 10, 4 }, { 5, 11 }, { 12, 8 }, { 9, 13 }, { 14, 8 }, { 9, 15 }, { 16, 10 }, { 11, 17 },
    { 18, 12 }, { 13, 19 }, { 20, 14 }, { 15, 21 }, { 22, 16 }, { 17, 23 }, { 24, 18 }, { 19, 25 },
    { 26, 18 }, { 19, 27 }, { 28, 22 }, { 23, 29 }, { 30, 22 }, { 23, 31 }, { 32, 24 }, { 25, 33 },
    { 34, 26 }, { 27, 35 }, { 36, 26 }, { 27, 37 }, { 38, 30 }, { 31, 39 }, { 40, 30 }, { 31, 41 },
    { 42, 32 }, { 33, 43 }, { 44, 32 }, { 33, 45 }, { 46, 36 }, { 37, 47 }, { 48, 36 }, { 37, 49 },
    { 50, 38 }, { 39, 51 }, { 52, 38 }, { 39, 53 }, { 54, 42 }, { 43, 55 }, { 56, 42 }, { 43, 57 },
    { 58, 44 }, { 45, 59 }, { 60, 44 }, { 45, 61 }, { 62, 46 }, { 47, 63 }, { 64, 48 }, { 49, 65 },
    { 66, 48 }, { 49, 67 }, { 68, 50 }, { 51, 69 }, { 70, 52 }, { 53, 71 }, { 72, 52 }, { 53, 73 },
    { 74, 54 }, { 55, 75 }, { 76, 54 }, { 55, 77 }, { 78, 56 }, { 57, 79 }, { 80, 58 }, { 59, 81 },
    { 82, 58 }, { 59, 83 }, { 84, 60 }, { 61, 85 }, { 86, 60 }, { 61, 87 }, { 88, 60 }, { 61, 89 },
    { 90, 62 }, { 63, 91 }, { 92, 64 }, { 65, 93 }, { 94, 64 }, { 65, 95 }, { 96, 66 }, { 67, 97 },
    { 98, 66 }, { 67, 99 }, { 100, 66 }, { 67, 101 }, { 102, 68 }, { 69, 103 }, { 104, 68 }, { 69, 105 },
    { 106, 70 }, { 71, 107 }, { 108, 70 }, { 71, 109 }, { 110, 70 }, { 71, 111 }, { 112, 72 }, { 73, 113 },
    { 114, 72 }, { 73, 115 }, { 116, 72 }, { 73, 117 }, { 118, 74 }, { 75, 119 }, { 120, 74 }, { 75, 121 },
    { 122, 74 }, { 75, 123 }, { 124, 76 }, { 77, 125 }, { 124, 76 }, { 77, 125 }, { 126, 126 }, { 127, 127 }
};

#define CB_CHUNK 32u

// Stable grouping by context: only the order within each context matters here.
__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void cb_bucket_hist(__global const uint4* ops, uint count,
                             __global uint* ranks, __global uint* hist,
                             __global uchar* models, __global uint* error) {
    uint lane = get_local_id(0), i = get_global_id(0);
    __local uint keys[256], counts[160];
    if (lane < 160) counts[lane] = 0;
    uint4 op = i < count ? ops[i] : (uint4)(3, 0, 0, 0);
    if (i < count) models[i] = 0;
    uint key = op.x == 0 && op.z < 160 && op.y <= 1 ? op.z : 160;
    keys[lane] = key;
    if (i < count && ((op.x == 0 && (op.y > 1 || op.z >= 160)) ||
        (op.x == 1 && (op.z > 32 || (op.z < 32 && (op.y >> op.z)))) ||
        (op.x == 2 && (op.y > 1 || (op.y && i + 1 != count))) || op.x > 2))
        atomic_or(error, 2u);
    if (i == 0 && (!count || ops[count - 1].x != 2 || ops[count - 1].y != 1))
        atomic_or(error, 2u);
    barrier(CLK_LOCAL_MEM_FENCE);
    if (key < 160) {
        uint rank = 0;
        for (uint j = 0; j < lane; j++) rank += keys[j] == key;
        ranks[i] = rank;
        atomic_inc(counts + key);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    if (lane < 160) hist[get_group_id(0) * 160 + lane] = counts[lane];
}

__kernel void cb_bucket_prefix(__global uint* hist, uint blocks,
                               __global uint* tile_totals) {
    uint index = get_global_id(0), c = index % 160, tile = index / 160;
    if (tile >= (blocks + 255) / 256) return;
    uint sum = 0;
    for (uint b = tile * 256; b < min(blocks, (tile + 1) * 256); b++) {
        uint n = hist[b * 160 + c]; hist[b * 160 + c] = sum; sum += n;
    }
    tile_totals[tile * 160 + c] = sum;
}

__kernel void cb_bucket_totals(__global uint* tile_totals, uint tiles, __global uint* bases,
                               __global const uchar* initial,
                               __global uchar* final_context) {
    uint c = get_global_id(0);
    if (c >= 160) return;
    uint sum = 0;
    for (uint b = 0; b < tiles; b++) {
        uint n = tile_totals[b * 160 + c]; tile_totals[b * 160 + c] = sum; sum += n;
    }
    bases[c] = sum;
    final_context[c] = initial[c];
}

__kernel void cb_bucket_bases(__global uint* bases) {
    if (get_global_id(0)) return;
    uint sum = 0;
    for (uint c = 0; c < 160; c++) { uint n = bases[c]; bases[c] = sum; sum += n; }
    bases[160] = sum;
}

__kernel void cb_bucket_scatter(__global const uint4* ops, uint count,
                                __global const uint* ranks, __global const uint* hist,
                                __global const uint* tile_totals,
                                __global const uint* bases, __global uint2* sorted) {
    uint i = get_global_id(0);
    if (i >= count) return;
    uint4 op = ops[i];
    if (op.x || op.z >= 160 || op.y > 1) return;
    uint at = bases[op.z] + hist[(i / 256) * 160 + op.z] + tile_totals[(i / 65536) * 160 + op.z] + ranks[i];
    sorted[at] = (uint2)(i, op.z * 2 + op.y);
}

// Each block produces a transition function for EVERY possible incoming model.
// A context change resets that function to the context's specified initial state.
__attribute__((reqd_work_group_size(128, 1, 1)))
__kernel void cb_context_maps(__global const uint2* sorted, __global const uint* bases,
                              __global const uchar* initial, __global uchar* maps) {
    uint lane = get_local_id(0), chunk = get_group_id(0), start = chunk * CB_CHUNK;
    uint n = bases[160];
    __local uint2 items[CB_CHUNK];
    __local uint next[256];
    next[lane * 2] = g_nextState[lane][0]; next[lane * 2 + 1] = g_nextState[lane][1];
    if (lane < CB_CHUNK && start + lane < n) items[lane] = sorted[start + lane];
    barrier(CLK_LOCAL_MEM_FENCE);
    uint state = lane;
    uint previous = start && start < n ? sorted[start - 1].y / 2 : 160;
    for (uint j = 0; j < min(CB_CHUNK, n > start ? n - start : 0); j++) {
        uint c = items[j].y / 2;
        if (c != previous) state = min((uint)initial[c], 125u);
        state = next[state * 2 + (items[j].y & 1)];
        previous = c;
    }
    maps[chunk * 128 + lane] = (uchar)state;
}

// Inclusive composition: right(left(state)); works for context and range maps.
__kernel void cb_compose(__global const uchar* input, __global uchar* output,
                         uint blocks, uint states, uint step) {
    uint i = get_global_id(0), block = i / states, state = i % states;
    if (block >= blocks) return;
    uint at = block >= step ? input[(block - step) * states + state] : state;
    output[i] = input[block * states + at];
}

// Scan 32 functions within a workgroup, then scan only the small tile summaries.
__kernel void cb_tile_maps(__global const uchar* input, __global uchar* local_prefix,
                           __global uchar* totals, uint blocks, uint states) {
    uint lane = get_local_id(0), tile = get_group_id(0), start = tile * 32;
    uint n = min(32u, blocks - start);
    __local uchar functions[32 * 256];
    for (uint b = 0; b < n; b++) functions[b * states + lane] = input[(start + b) * states + lane];
    barrier(CLK_LOCAL_MEM_FENCE);
    uint state = lane;
    for (uint b = 0; b < n; b++) {
        state = functions[b * states + state];
        local_prefix[(start + b) * states + lane] = (uchar)state;
    }
    totals[tile * states + lane] = (uchar)state;
}

uint prefix_value(__global const uchar* local_prefix, __global const uchar* tiles,
                  uint block, uint states, uint initial) {
    uint tile = block / 32;
    uint incoming = tile ? tiles[(tile - 1) * states + initial] : initial;
    return local_prefix[block * states + incoming];
}

__kernel void cb_context_emit(__global const uint2* sorted, __global const uint* bases,
                              __global const uchar* initial, __global const uchar* prefix,
                              __global const uchar* tiles,
                              __global uchar* models, __global uchar* final_context,
                              __global const uint4* ops, uint validate, __global uint* error) {
    uint chunk = get_global_id(0), start = chunk * CB_CHUNK, n = bases[160];
    __local uint next[256];
    for (uint j = get_local_id(0); j < 256; j += get_local_size(0)) next[j] = g_nextState[j / 2][j % 2];
    barrier(CLK_LOCAL_MEM_FENCE);
    if (start >= n) return;
    uint state = chunk ? prefix_value(prefix, tiles, chunk - 1, 128, 0) : 0;
    uint previous = start ? sorted[start - 1].y / 2 : 160;
    for (uint j = start; j < min(n, start + CB_CHUNK); j++) {
        uint2 item = sorted[j]; uint c = item.y / 2;
        if (c != previous) state = initial[c];
        if (state >= 126) { atomic_or(error, 4u); state = 124; }
        if (validate && state != ops[item.x].w) atomic_or(error, 8u);
        models[item.x] = (uchar)state;
        state = next[state * 2 + (item.y & 1)];
        if (j + 1 == n || sorted[j + 1].y / 2 != c) final_context[c] = (uchar)state;
        previous = c;
    }
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void cb_range_maps(__global const uint4* ops, uint count,
                            __global const uchar* models, __global uchar* maps) {
    uint lane = get_local_id(0), chunk = get_group_id(0), start = chunk * CB_CHUNK;
    __local uint4 items[CB_CHUNK];
    __local uint lps_table[256];
    lps_table[lane] = g_lpsTable[lane / 4][lane % 4];
    if (lane < CB_CHUNK && start + lane < count) {
        uint4 op = ops[start + lane];
        if (op.x == 0) op.w = min((uint)models[start + lane], 125u);
        items[lane] = op;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    uint range = lane + 256;
    for (uint j = 0; j < min(CB_CHUNK, count > start ? count - start : 0); j++) {
        uint4 op = items[j];
        if (op.x == 0) {
            uint lps = lps_table[(op.w / 2) * 4 + ((range & 255) >> 6)];
            range -= lps;
            if ((op.y ^ op.w) & 1) range = lps << (clz(lps) - 23);
            else if (range < 256) range *= 2;
        } else if (op.x == 2) {
            range -= 2;
            if (op.y) range = 256;
            else if (range < 256) range *= 2;
        }
    }
    maps[chunk * 256 + lane] = (uchar)(range - 256);
}

// Once each block's incoming range is known, produce independent additive terms:
// low_after = (low_before << shift) + additive. No serial low register is needed.
__kernel void cb_terms(__global const uint4* ops, uint count, __global const uchar* models,
                       __global const uchar* prefix, __global const uchar* tiles, __global ulong* terms,
                       __global uint* offsets, __global uint* block_bits) {
    uint chunk = get_global_id(0), start = chunk * CB_CHUNK;
    __local uint lps_table[256];
    for (uint j = get_local_id(0); j < 256; j += get_local_size(0)) lps_table[j] = g_lpsTable[j / 4][j % 4];
    barrier(CLK_LOCAL_MEM_FENCE);
    if (chunk >= max(1u, (count + CB_CHUNK - 1) / CB_CHUNK)) return;
    uint range = chunk ? prefix_value(prefix, tiles, chunk - 1, 256, 254) + 256 : 510;
    uint bits = 0;
    for (uint i = start; i < min(count, start + CB_CHUNK); i++) {
        uint4 op = ops[i]; uint shift = 0; ulong add = 0;
        if (op.x == 0) {
            uint model = min((uint)models[i], 125u);
            uint lps = lps_table[(model / 2) * 4 + ((range & 255) >> 6)];
            range -= lps;
            if ((op.y ^ model) & 1) { shift = clz(lps) - 23; add = (ulong)range << shift; range = lps << shift; }
            else if (range < 256) { range *= 2; shift = 1; }
        } else if (op.x == 1) { shift = min(op.z, 32u); add = (ulong)range * op.y; }
        else if (op.x == 2) {
            range -= 2;
            if (op.y) { add = (ulong)range << 7; range = 256; shift = 7; }
            else if (range < 256) { range *= 2; shift = 1; }
        }
        bits += shift; offsets[i] = bits; terms[i] = add;
    }
    block_bits[chunk] = bits;
}

uint scan_combine(uint left, uint right, uint carry_mode) {
    return carry_mode ? ((right & 1) | ((right >> 1) & (left & 1)) | (right & left & 2)) : left + right;
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void cb_scan_tiles(__global const uint* input, __global uint* prefix,
                            __global uint* totals, uint count, uint carry_mode) {
    uint i = get_global_id(0), lane = get_local_id(0), identity = carry_mode ? 2 : 0;
    __local uint shared[256];
    shared[lane] = i < count ? input[i] : identity;
    barrier(CLK_LOCAL_MEM_FENCE);
    for (uint step = 1; step < 256; step *= 2) {
        uint value = scan_combine(lane >= step ? shared[lane - step] : identity, shared[lane], carry_mode);
        barrier(CLK_LOCAL_MEM_FENCE);
        shared[lane] = value;
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (i < count) prefix[i] = shared[lane];
    if (lane == 255) totals[get_group_id(0)] = shared[lane];
}

__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void cb_scan_totals(__global const uint* input, __global uint* prefix,
                             uint count, uint carry_mode) {
    uint lane = get_local_id(0), width = (count + 255) / 256, start = lane * width;
    uint identity = carry_mode ? 2 : 0, value = identity;
    __local uint shared[256];
    for (uint i = start; i < min(count, start + width); i++) value = scan_combine(value, input[i], carry_mode);
    shared[lane] = value;
    barrier(CLK_LOCAL_MEM_FENCE);
    for (uint step = 1; step < 256; step *= 2) {
        uint sum = scan_combine(lane >= step ? shared[lane - step] : identity, shared[lane], carry_mode);
        barrier(CLK_LOCAL_MEM_FENCE);
        shared[lane] = sum;
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    value = lane ? shared[lane - 1] : identity;
    for (uint i = start; i < min(count, start + width); i++) {
        value = scan_combine(value, input[i], carry_mode); prefix[i] = value;
    }
}

__kernel void cb_scan_add(__global const uint* local_prefix, __global const uint* tiles,
                          __global uint* prefix, uint count, uint carry_mode) {
    uint i = get_global_id(0), tile = i / 256;
    if (i < count) prefix[i] = scan_combine(tile ? tiles[tile - 1] : (carry_mode ? 2 : 0), local_prefix[i], carry_mode);
}

__kernel void cb_add_digits(__global const ulong* terms, __global const uint* offsets,
                            __global const uint* bits, uint chunks, uint count,
                            __global uint* digits, uint digit_count, __global uint* error) {
    uint i = get_global_id(0);
    if (i >= count || !terms[i]) return;
    uint chunk = i / CB_CHUNK;
    uint position = bits[chunks - 1] - offsets[i] - (chunk ? bits[chunk - 1] : 0);
    uint byte = position / 8;
    ulong value = terms[i] << (position % 8);
    while (value) {
        if (byte < digit_count) atomic_add(digits + byte, (uint)value & 255);
        else atomic_or(error, 1u);
        byte++; value >>= 8;
    }
}

// Carry recurrence e[i] = generate[i] | (propagate[i] & e[i-1]).
__kernel void cb_carry_maps(__global const uint* digits, uint digit_count,
                           __global uint* maps) {
    uint i = get_global_id(0);
    if (i >= digit_count) return;
    uint value = (digits[i] & 255) + (i ? digits[i - 1] >> 8 : 0);
    maps[i] = (value >= 256) | ((value == 255) << 1);
}

uint normalized_digit(__global const uint* digits, __global const uint* carry, uint i) {
    return (digits[i] + (i ? (digits[i - 1] >> 8) + (carry[i - 1] & 1) : 0)) & 255;
}

__kernel void cb_pack(__global const uint* digits, __global const uint* carry,
                      __global const uint* bits, uint chunks, uint count,
                      __global uchar* output, uint capacity, uint digit_count,
                      __global const uint* error, __global uint* result) {
    uint i = get_global_id(0), total = bits[chunks - 1];
    uint bytes = (total + 9) / 8;
    uint status = *error;
    if (bytes > capacity || bytes + 1 >= digit_count) status |= 1;
    if (i == 0) { result[0] = bytes; result[1] = status; result[2] = count; }
    if (status || i >= bytes) return;
    uint pad = bytes * 8 - (total + 2), shift = pad + 1;
    uint hi = normalized_digit(digits, carry, i + 1);
    uint lo = i ? normalized_digit(digits, carry, i) : 0;
    uint byte = (hi << shift) | (lo >> (8 - shift));
    if (!i) byte |= 1u << pad;
    output[bytes - 1 - i] = (uchar)byte;
}
