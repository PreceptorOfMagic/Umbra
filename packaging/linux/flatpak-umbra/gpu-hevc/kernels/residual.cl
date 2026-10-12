/* SPDX-License-Identifier: GPL-2.0-or-later
 * x265-derived residual / transform / quantization / reconstruction primitives.
 * Based on x265 b81f650e21e8aacbe6a9ad04ce14aefc05b932c0:
 * source/common/{constants.cpp,dct.cpp,quant.cpp,scalinglist.cpp}.
 * Copyright (C) 2013-2020 MulticoreWare, Inc; x265 contributors.
 * Aurora OpenCL adaptation, 2026. See COPYING.
 * Input prediction is supplied by the caller; motion search is not implemented.
 */
__constant short g_t4[4][4] =
{
    { 64, 64, 64, 64 },
    { 83, 36, -36, -83 },
    { 64, -64, -64, 64 },
    { 36, -83, 83, -36 }
};
__constant short g_t8[8][8] =
{
    { 64, 64, 64, 64, 64, 64, 64, 64 },
    { 89, 75, 50, 18, -18, -50, -75, -89 },
    { 83, 36, -36, -83, -83, -36, 36, 83 },
    { 75, -18, -89, -50, 50, 89, 18, -75 },
    { 64, -64, -64, 64, 64, -64, -64, 64 },
    { 50, -89, 18, 75, -75, -18, 89, -50 },
    { 36, -83, 83, -36, -36, 83, -83, 36 },
    { 18, -50, 75, -89, 89, -75, 50, -18 }
};
__constant short g_t16[16][16] =
{
    { 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
    { 90, 87, 80, 70, 57, 43, 25,  9, -9, -25, -43, -57, -70, -80, -87, -90 },
    { 89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89 },
    { 87, 57,  9, -43, -80, -90, -70, -25, 25, 70, 90, 80, 43, -9, -57, -87 },
    { 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83 },
    { 80,  9, -70, -87, -25, 57, 90, 43, -43, -90, -57, 25, 87, 70, -9, -80 },
    { 75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75 },
    { 70, -43, -87,  9, 90, 25, -80, -57, 57, 80, -25, -90, -9, 87, 43, -70 },
    { 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64 },
    { 57, -80, -25, 90, -9, -87, 43, 70, -70, -43, 87,  9, -90, 25, 80, -57 },
    { 50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50 },
    { 43, -90, 57, 25, -87, 70,  9, -80, 80, -9, -70, 87, -25, -57, 90, -43 },
    { 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36 },
    { 25, -70, 90, -80, 43,  9, -57, 87, -87, 57, -9, -43, 80, -90, 70, -25 },
    { 18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18 },
    {  9, -25, 43, -57, 70, -80, 87, -90, 90, -87, 80, -70, 57, -43, 25, -9 }
};
__constant short g_t32[32][32] =
{
    { 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64 },
    { 90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13,  4, -4, -13, -22, -31, -38, -46, -54, -61, -67, -73, -78, -82, -85, -88, -90, -90 },
    { 90, 87, 80, 70, 57, 43, 25,  9, -9, -25, -43, -57, -70, -80, -87, -90, -90, -87, -80, -70, -57, -43, -25, -9,  9, 25, 43, 57, 70, 80, 87, 90 },
    { 90, 82, 67, 46, 22, -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13, 13, 38, 61, 78, 88, 90, 85, 73, 54, 31,  4, -22, -46, -67, -82, -90 },
    { 89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89, 89, 75, 50, 18, -18, -50, -75, -89, -89, -75, -50, -18, 18, 50, 75, 89 },
    { 88, 67, 31, -13, -54, -82, -90, -78, -46, -4, 38, 73, 90, 85, 61, 22, -22, -61, -85, -90, -73, -38,  4, 46, 78, 90, 82, 54, 13, -31, -67, -88 },
    { 87, 57,  9, -43, -80, -90, -70, -25, 25, 70, 90, 80, 43, -9, -57, -87, -87, -57, -9, 43, 80, 90, 70, 25, -25, -70, -90, -80, -43,  9, 57, 87 },
    { 85, 46, -13, -67, -90, -73, -22, 38, 82, 88, 54, -4, -61, -90, -78, -31, 31, 78, 90, 61,  4, -54, -88, -82, -38, 22, 73, 90, 67, 13, -46, -85 },
    { 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83, 83, 36, -36, -83, -83, -36, 36, 83 },
    { 82, 22, -54, -90, -61, 13, 78, 85, 31, -46, -90, -67,  4, 73, 88, 38, -38, -88, -73, -4, 67, 90, 46, -31, -85, -78, -13, 61, 90, 54, -22, -82 },
    { 80,  9, -70, -87, -25, 57, 90, 43, -43, -90, -57, 25, 87, 70, -9, -80, -80, -9, 70, 87, 25, -57, -90, -43, 43, 90, 57, -25, -87, -70,  9, 80 },
    { 78, -4, -82, -73, 13, 85, 67, -22, -88, -61, 31, 90, 54, -38, -90, -46, 46, 90, 38, -54, -90, -31, 61, 88, 22, -67, -85, -13, 73, 82,  4, -78 },
    { 75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75, 75, -18, -89, -50, 50, 89, 18, -75, -75, 18, 89, 50, -50, -89, -18, 75 },
    { 73, -31, -90, -22, 78, 67, -38, -90, -13, 82, 61, -46, -88, -4, 85, 54, -54, -85,  4, 88, 46, -61, -82, 13, 90, 38, -67, -78, 22, 90, 31, -73 },
    { 70, -43, -87,  9, 90, 25, -80, -57, 57, 80, -25, -90, -9, 87, 43, -70, -70, 43, 87, -9, -90, -25, 80, 57, -57, -80, 25, 90,  9, -87, -43, 70 },
    { 67, -54, -78, 38, 85, -22, -90,  4, 90, 13, -88, -31, 82, 46, -73, -61, 61, 73, -46, -82, 31, 88, -13, -90, -4, 90, 22, -85, -38, 78, 54, -67 },
    { 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64, 64, -64, -64, 64 },
    { 61, -73, -46, 82, 31, -88, -13, 90, -4, -90, 22, 85, -38, -78, 54, 67, -67, -54, 78, 38, -85, -22, 90,  4, -90, 13, 88, -31, -82, 46, 73, -61 },
    { 57, -80, -25, 90, -9, -87, 43, 70, -70, -43, 87,  9, -90, 25, 80, -57, -57, 80, 25, -90,  9, 87, -43, -70, 70, 43, -87, -9, 90, -25, -80, 57 },
    { 54, -85, -4, 88, -46, -61, 82, 13, -90, 38, 67, -78, -22, 90, -31, -73, 73, 31, -90, 22, 78, -67, -38, 90, -13, -82, 61, 46, -88,  4, 85, -54 },
    { 50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50, 50, -89, 18, 75, -75, -18, 89, -50, -50, 89, -18, -75, 75, 18, -89, 50 },
    { 46, -90, 38, 54, -90, 31, 61, -88, 22, 67, -85, 13, 73, -82,  4, 78, -78, -4, 82, -73, -13, 85, -67, -22, 88, -61, -31, 90, -54, -38, 90, -46 },
    { 43, -90, 57, 25, -87, 70,  9, -80, 80, -9, -70, 87, -25, -57, 90, -43, -43, 90, -57, -25, 87, -70, -9, 80, -80,  9, 70, -87, 25, 57, -90, 43 },
    { 38, -88, 73, -4, -67, 90, -46, -31, 85, -78, 13, 61, -90, 54, 22, -82, 82, -22, -54, 90, -61, -13, 78, -85, 31, 46, -90, 67,  4, -73, 88, -38 },
    { 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36, 36, -83, 83, -36, -36, 83, -83, 36 },
    { 31, -78, 90, -61,  4, 54, -88, 82, -38, -22, 73, -90, 67, -13, -46, 85, -85, 46, 13, -67, 90, -73, 22, 38, -82, 88, -54, -4, 61, -90, 78, -31 },
    { 25, -70, 90, -80, 43,  9, -57, 87, -87, 57, -9, -43, 80, -90, 70, -25, -25, 70, -90, 80, -43, -9, 57, -87, 87, -57,  9, 43, -80, 90, -70, 25 },
    { 22, -61, 85, -90, 73, -38, -4, 46, -78, 90, -82, 54, -13, -31, 67, -88, 88, -67, 31, 13, -54, 82, -90, 78, -46,  4, 38, -73, 90, -85, 61, -22 },
    { 18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18, 18, -50, 75, -89, 89, -75, 50, -18, -18, 50, -75, 89, -89, 75, -50, 18 },
    { 13, -38, 61, -78, 88, -90, 85, -73, 54, -31,  4, 22, -46, 67, -82, 90, -90, 82, -67, 46, -22, -4, 31, -54, 73, -85, 90, -88, 78, -61, 38, -13 },
    {  9, -25, 43, -57, 70, -80, 87, -90, 90, -87, 80, -70, 57, -43, 25, -9, -9, 25, -43, 57, -70, 80, -87, 90, -90, 87, -80, 70, -57, 43, -25,  9 },
    {  4, -13, 22, -31, 38, -46, 54, -61, 67, -73, 78, -82, 85, -88, 90, -90, 90, -90, 88, -85, 82, -78, 73, -67, 61, -54, 46, -38, 31, -22, 13, -4 }
};

__constant short g_dst4[4][4] = {
    {29,55,74,84}, {74,74,0,-74}, {84,-29,-74,55}, {55,-84,74,-29}
};
__constant int quant_scales[6] = {26214,23302,20560,18396,16384,14564};
__constant int inverse_scales[6] = {40,45,51,57,64,72};
int transform_weight(uint n, uint dst, uint row, uint col) {
    if (n == 4) return dst ? g_dst4[row][col] : g_t4[row][col];
    if (n == 8) return g_t8[row][col];
    if (n == 16) return g_t16[row][col];
    return g_t32[row][col];
}
int round_shift(int value, uint shift) { return (value + (1 << (shift - 1))) >> shift; }

// One TU per workgroup. Quantized coefficients and reconstruction remain GPU
// resident for downstream syntax/reference use. Debug output is optional.
__attribute__((reqd_work_group_size(256, 1, 1)))
__kernel void residual_encode(__global const ushort* input,
                              __global const ushort* prediction,
                              __global const uint4* parameters,
                              uint blocks, uint configured_lg, uint configured_depth, uint validate,
                              __global short* quantized, __global ushort* reconstructed,
                              __global short* diagnostics, __global uint* errors
#ifdef FRAME_PREDICTION
                              , uint frame_width, uint frame_height, uint diagonal, uint intra, uint frame_qp,
                              __global const uint* cu_qps, uint adaptive_qp, __global const uint* intra_modes, uint isolated_intra
#endif
                              ) {
    uint block = get_group_id(0), lane = get_local_id(0);
#ifdef FRAME_PREDICTION
    // Complete-picture development path: unsplit CTB32 / chroma16, DC intra
    // or motion-compensated single-reference inter. No pixel coding on the CPU.
    const uint side = 1u << TU_LOG2;
    if (!frame_width || !frame_height || frame_width%side || frame_height%side ||
        blocks!=(frame_width/side)*(frame_height/side) || intra>1) {
        if (!block && !lane) atomic_or(errors,4u);
        return;
    }
    if (intra) {
        uint bx=block;
        if (bx>diagonal || bx>=frame_width/side || diagonal-bx>=frame_height/side) return;
        block=(diagonal-bx)*(frame_width/side)+bx;
    }
#endif
    if (block >= blocks) return;
#ifdef FRAME_PREDICTION
    // Split P reconstruction by the isolated 2x2 eligibility grid. Neighbor CUs
    // finish first; eligible CUs then encode exactly once as either intra or inter.
    // The branch is uniform within a workgroup and precedes every barrier.
    if (!intra && (diagonal&0xc0000000u)) {
        uint columns=frame_width/(1u<<TU_LOG2),phase=diagonal&3u;
        uint eligible=(block%columns)%2==phase%2 && (block/columns)%2==phase/2;
        if(((diagonal&0xc0000000u)==0x40000000u && eligible) ||
           ((diagonal&0xc0000000u)==0x80000000u && !eligible))return;
    }
#endif
    const uint lg = TU_LOG2, depth = PIXEL_DEPTH;
#ifdef FRAME_PREDICTION
    uint4 p = (uint4)(frame_qp,intra,0,0);
    if(adaptive_qp) {
        uint q=cu_qps[block];
        // This picture profile has TU32 luma and TU16 4:2:0 chroma.
        const uint scale[22]={29,30,31,32,33,33,34,34,35,35,36,36,37,37,38,39,40,41,42,43,44,45};
        p.x=(TU_LOG2==4 && q>=30)?scale[q-30]:q;
    }
#else
    uint4 p = parameters[block]; // base QP, I-slice rounding, 4x4 DST, reserved
#endif
    if (configured_lg != lg || configured_depth != depth || lg < 2 || lg > 5 ||
        (depth != 8 && depth != 10) || p.x > 51 || p.y > 1 ||
        p.z > 1 || (p.z && lg != 2) || p.w) {
        if (!lane) atomic_or(errors, 1u);
        return;
    }
    uint n = 1u << lg, area = n * n, base = block * area;
    uint total = blocks * area, maximum = (1u << depth) - 1;
    __local short residual[1024], first[1024], coefficients[1024];
    __local short weights[1024], transposed[1024];
#ifdef FRAME_PREDICTION
    __local ushort frame_pred[1024];
    __local uint dc;
    uint local_intra=intra || (isolated_intra && intra_modes[block]);
    uint bx=block%(frame_width/n), by=block/(frame_width/n);
    uint origin=by*n*frame_width+bx*n;
    if (local_intra && !lane) {
        uint sum=n;
        for (uint k=0;k<n;k++) {
            uint top=by?reconstructed[origin-frame_width+k]:
                     (bx?reconstructed[origin-1]:(1u<<(depth-1)));
            uint left=bx?reconstructed[origin+k*frame_width-1]:
                      (by?reconstructed[origin-frame_width]:(1u<<(depth-1)));
            sum+=top+left;
        }
        dc=sum/(2*n);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
#endif
    for (uint i = lane; i < area; i += 256) {
        short weight = (short)transform_weight(n, p.z, i/n, i%n);
        weights[i] = weight;
        transposed[(i%n)*n+i/n] = weight;
#ifdef FRAME_PREDICTION
        uint pixel_at=origin+(i/n)*frame_width+i%n;
        ushort a=input[pixel_at], b=local_intra?(ushort)dc:prediction[base+i];
        frame_pred[i]=b;
#else
        ushort a = input[base + i], b = prediction[base + i];
#endif
        if (a > maximum || b > maximum) atomic_or(errors, 2u);
        residual[i] = (short)((int)min((uint)a, maximum) - (int)min((uint)b, maximum));
        if (validate) diagnostics[base + i] = residual[i];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    // Forward horizontal then vertical integer transform, preserving x265 rounding.
    for (uint i = lane; i < area; i += 256) {
        uint y = i / n, u = i % n;
        int sum = 0;
        for (uint x = 0; x < n; x++) sum += transposed[x*n+u] * residual[y*n+x];
        first[i] = (short)round_shift(sum, lg - 1 + depth - 8);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    int effective = p.x + 6 * (depth - 8), per = effective / 6, rem = effective % 6;
    int ts = 15 - (int)depth - (int)lg, qbits = 14 + per + ts;
    int add = (p.y ? 171 : 85) << (qbits - 9);
    int inverse_scale = inverse_scales[rem] << per;
    for (uint i = lane; i < area; i += 256) {
        uint v = i / n, u = i % n;
        int sum = 0;
        for (uint y = 0; y < n; y++) sum += weights[v*n+y] * first[y*n+u];
        int value = (short)round_shift(sum, lg + 6);
        int level = ((int)abs(value) * quant_scales[rem] + add) >> qbits;
        level = clamp(value < 0 ? -level : level, -32768, 32767);
        quantized[base+i] = (short)level;
        int dq = clamp(round_shift(level * inverse_scale, 6 - ts), -32768, 32767);
        coefficients[i] = (short)dq;
        if (validate) {
            diagnostics[total+base+i] = (short)value;
            diagnostics[2*total+base+i] = (short)dq;
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    // Inverse vertical then horizontal; each intermediate clips to signed 16 bits.
    for (uint i = lane; i < area; i += 256) {
        uint y = i / n, u = i % n;
        int sum = 0;
        for (uint v = 0; v < n; v++) sum += transposed[y*n+v] * coefficients[v*n+u];
        first[i] = (short)clamp(round_shift(sum, 7), -32768, 32767);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for (uint i = lane; i < area; i += 256) {
        uint y = i / n, x = i % n;
        int sum = 0;
        for (uint u = 0; u < n; u++) sum += weights[u*n+x] * first[y*n+u];
        int value = clamp(round_shift(sum, 20 - depth), -32768, 32767);
#ifdef FRAME_PREDICTION
        reconstructed[origin+y*frame_width+x]=(ushort)clamp((int)frame_pred[i]+value,0,(int)maximum);
#else
        reconstructed[base+i] = (ushort)clamp((int)prediction[base+i] + value, 0, (int)maximum);
#endif
        if (validate) diagnostics[3*total+base+i] = (short)value;
    }
}
