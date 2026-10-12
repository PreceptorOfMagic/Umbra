// SPDX-License-Identifier: GPL-2.0-or-later
// Unweighted, single-reference inter prediction adapted from x265
// b81f650e21e8aacbe6a9ad04ce14aefc05b932c0:
// common/predict.cpp:279-389, ipfilter.cpp:80-284, constants.cpp:250-268.
// Copyright (C) 2013-2020 MulticoreWare, Inc (upstream algorithms).
// Host encoder primitive only: no composition, POC rewriting or co-op state.
// BLOCK_LOG2, PIXEL_DEPTH and CHROMA select the compiled plane variant.
#if (PIXEL_DEPTH != 8 && PIXEL_DEPTH != 10) || BLOCK_LOG2 < 2 || BLOCK_LOG2 > 5
#error Unsupported prediction variant
#endif
#if CHROMA != 0 && CHROMA != 1
#error Unsupported plane
#endif
#define PN (1 << BLOCK_LOG2)
#define PT (CHROMA ? 4 : 8)
#define PB (PT / 2 - 1)
#define PF (CHROMA ? 3 : 2)
__constant short p_luma[4][8] = {
    {0,0,0,64,0,0,0,0}, {-1,4,-10,58,17,-5,1,0},
    {-1,4,-11,40,40,-11,4,-1}, {0,1,-5,17,58,-10,4,-1}
};
__constant short p_chroma[8][4] = {
    {0,64,0,0}, {-2,58,10,-2}, {-4,54,16,-2}, {-6,46,28,-4},
    {-4,36,36,-4}, {-4,28,46,-6}, {-2,16,54,-4}, {-2,10,58,-2}
};
inline int p_coeff(int phase, int tap) {
    return CHROMA ? p_chroma[phase][tap] : p_luma[phase][tap];
}
inline int p_sample(__global const ushort* ref, uint width, uint height, int x, int y) {
    // Replicated reference margins, not scaling/cropping the captured picture.
    return ref[(size_t)clamp(y,0,(int)height-1)*width+clamp(x,0,(int)width-1)];
}
__kernel void inter_predict(__global const ushort* ref, uint width, uint height,
                            __global const int4* blocks, uint count,
                            __global ushort* prediction, __global uint* error) {
    uint block=get_group_id(0), lane=get_local_id(0);
    if (block>=count) return;
    int4 b=blocks[block]; // plane-space origin x/y; signed quarter-luma-sample MV x/y
    if (width<PN || height<PN || width>8192 || height>8192 ||
        b.x<0 || b.y<0 || b.x>(int)width-PN || b.y>(int)height-PN ||
        b.z < -32768 || b.z > 32767 || b.w < -32768 || b.w > 32767) {
        if (!lane) atomic_or(error,1u);
        return;
    }
    int ox=b.x+(b.z>>PF), oy=b.y+(b.w>>PF);
    int fx=b.z&((1<<PF)-1), fy=b.w&((1<<PF)-1);
    __local short intermediate[PN*(PN+PT-1)];
    if (fx && fy) {
        // Preserve the signed internal precision between the two filters.
        for (uint i=lane;i<PN*(PN+PT-1);i+=get_local_size(0)) {
            int x=i%PN, y=i/PN, sum=0;
            for (int k=0;k<PT;k++) sum+=p_coeff(fx,k)*p_sample(ref,width,height,ox+x+k-PB,oy+y-PB);
            intermediate[i]=(short)((sum-(8192<<(PIXEL_DEPTH-8)))>>(PIXEL_DEPTH-8));
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    for (uint i=lane;i<PN*PN;i+=get_local_size(0)) {
        int x=i%PN, y=i/PN, value=0;
        if (fx && fy) {
            for (int k=0;k<PT;k++) value+=p_coeff(fy,k)*intermediate[(y+k)*PN+x];
            const int shift=6+14-PIXEL_DEPTH;
            value=(value+(1<<(shift-1))+(8192<<6))>>shift;
        } else if (fx || fy) {
            for (int k=0;k<PT;k++) value+=p_coeff(fx?fx:fy,k)*
                p_sample(ref,width,height,ox+x+(fx?k-PB:0),oy+y+(fy?k-PB:0));
            value=(value+32)>>6;
        } else value=p_sample(ref,width,height,ox+x,oy+y);
        prediction[(size_t)block*PN*PN+i]=(ushort)clamp(value,0,(1<<PIXEL_DEPTH)-1);
    }
}
