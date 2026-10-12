/* SPDX-License-Identifier: GPL-2.0-or-later
 * CABAC arithmetic coding port from x265 b81f650e21e8aacbe6a9ad04ce14aefc05b932c0.
 * Copyright (C) 2013-2020 MulticoreWare, Inc.
 * Original authors: Steve Borho, Min Chen (entropy.cpp), x265 contributors.
 * Aurora OpenCL adaptation, 2026. See COPYING and README.md.
 * This kernel is one component, not a complete HEVC encoder.
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

typedef struct {
    uint low, range, buffered, pending, size, capacity, error;
    int bits_left;
} Cabac;

void put_byte(__private Cabac* c, __global uchar* out, uint byte) {
    if (c->size < c->capacity) out[c->size] = (uchar)byte;
    else c->error = 1;
    c->size++;
}

void write_out(__private Cabac* c, __global uchar* out) {
    uint lead = c->low >> (13 + c->bits_left);
    uint mask = 0xffffffffu >> (19 - c->bits_left);
    c->bits_left -= 8;
    c->low &= mask;
    if (lead == 0xff) c->pending++;
    else {
        if (c->pending) {
            uint carry = lead >> 8;
            put_byte(c, out, c->buffered + carry);
            while (c->pending > 1) {
                put_byte(c, out, (0xff + carry) & 0xff);
                c->pending--;
            }
        }
        c->pending = 1;
        c->buffered = lead & 0xff;
    }
}

void bypass(__private Cabac* c, __global uchar* out, uint value, uint count) {
    while (count > 8) {
        count -= 8;
        uint pattern = value >> count;
        c->low = (c->low << 8) + c->range * pattern;
        value -= pattern << count;
        c->bits_left += 8;
        if (c->bits_left >= 0) write_out(c, out);
    }
    c->low = (c->low << count) + c->range * value;
    c->bits_left += count;
    if (c->bits_left >= 0) write_out(c, out);
}

// A single dependent CABAC chain, one slice, no WPP, no frame batching.
// One workgroup stages operations and tables into local memory; lane 0 codes.
// Operations are uint4(kind, value, context_or_bit_count, oracle_pre_state).
// Validation may be disabled for timing; final output remains checked by the host.
__attribute__((reqd_work_group_size(64, 1, 1)))
__kernel void cabac_encode(__global const uint4* ops, uint count,
                           __global const uchar* initial,
                           __global uchar* output, uint capacity,
                           __global uchar* final_context,
                           __global uint* result, uint validate,
                           __global Cabac* saved, uint begin, uint end) {
    if (get_group_id(0) != 0) return;
    uint lane = get_local_id(0);
    __local uchar ctx[160], lps_table[64][4], next_state[128][2];
    __local uint4 staged[256];
    for (uint j = lane; j < 160; j += 64) ctx[j] = begin ? final_context[j] : initial[j];
    for (uint j = lane; j < 256; j += 64) {
        lps_table[j >> 2][j & 3] = g_lpsTable[j >> 2][j & 3];
        next_state[j >> 1][j & 1] = g_nextState[j >> 1][j & 1];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    Cabac c = {0, 510, 0xff, 0, 0, capacity, 0, -12};
    if (begin) c = *saved;
    if (end > count || begin > end) c.error = 2;
    uint i = begin;
    for (uint base = begin; base < min(end, count); base += 256) {
      for (uint j = lane; j < min(256u, min(end, count) - base); j += 64) staged[j] = ops[base + j];
      barrier(CLK_LOCAL_MEM_FENCE);
      if (lane == 0 && !c.error) {
       for (i = base; i < min(end, base + 256) && !c.error; i++) {
        uint4 op = staged[i - base];
        if (op.x == 0) {
            if (op.z >= 160 || op.y > 1) { c.error = 2; break; }
            uint model = ctx[op.z];
            if (model >= 128 || (validate && model != op.w)) { c.error = 3; break; }
            ctx[op.z] = next_state[model][op.y];
            uint state = model >> 1;
            uint lps = lps_table[state][(c.range & 0xff) >> 6];
            uint range = c.range - lps;
            uint low = c.low;
            uint n = (range - 256u) >> 31;
            if ((op.y ^ model) & 1) {
                n = state >= 63 ? 6 : clz(lps) - 23;
                low += range;
                range = lps;
            }
            c.low = low << n;
            c.range = range << n;
            c.bits_left += n;
        } else if (op.x == 1) {
            if (op.z > 32 || (op.z < 32 && (op.y >> op.z))) { c.error = 2; break; }
            bypass(&c, output, op.y, op.z);
        } else if (op.x == 2) {
            if (op.y > 1 || (op.y == 1 && i + 1 != count)) { c.error = 2; break; }
            c.range -= 2;
            if (op.y) {
                c.low = (c.low + c.range) << 7;
                c.range = 256;
                c.bits_left += 7;
            } else if (c.range < 256) {
                c.low <<= 1;
                c.range <<= 1;
                c.bits_left++;
            }
        } else { c.error = 2; break; }
        if (c.bits_left >= 0) write_out(&c, output);
       }
      }
      barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (lane != 0) return;
    if (!count || ops[count - 1].x != 2 || ops[count - 1].y != 1) c.error = 2;
    if (!c.error && end == count) {
        if (c.low >> (21 + c.bits_left)) {
            put_byte(&c, output, c.buffered + 1);
            while (c.pending > 1) { put_byte(&c, output, 0); c.pending--; }
            c.low -= 1u << (21 + c.bits_left);
        } else {
            if (c.pending) put_byte(&c, output, c.buffered);
            while (c.pending > 1) { put_byte(&c, output, 0xff); c.pending--; }
        }
        // x265 finish() followed by writeByteAlignment(): residual bits, 1, zeros.
        uint n = 13 + c.bits_left;
        uint tail = ((c.low >> 8) << 1) | 1;
        n++;
        uint padded = (n + 7) & ~7u;
        tail <<= padded - n;
        for (int shift = (int)padded - 8; shift >= 0; shift -= 8)
            put_byte(&c, output, tail >> shift);
    }
    for (uint j = 0; j < 160; j++) final_context[j] = ctx[j];
    result[0] = c.size;
    result[1] = c.error;
    result[2] = i;
    *saved = c;
}
