/* SPDX-License-Identifier: GPL-2.0-or-later
 * Coefficient binarization adapted from x265 b81f650e21e8aacbe6a9ad04ce14aefc05b932c0:
 * encoder/entropy.cpp codeCoeffNxN/writeCoefRemainExGolomb,
 * common/constants.cpp and common/quant.h.
 * Copyright (C) 2013-2020 MulticoreWare, Inc; x265 contributors.
 * Aurora OpenCL adaptation, 2026. See COPYING.
 * No sign-data hiding, transform skip syntax, or CU/TU tree syntax in this component.
 */
__constant ushort g_scan2x2[][2*2] =
{
    { 0, 2, 1, 3 },
    { 0, 1, 2, 3 },
};
__constant ushort g_scan8x8diag[8 * 8] =
{
    0,   8,  1, 16,  9,  2, 24, 17,
    10,  3, 32, 25, 18, 11,  4, 40,
    33, 26, 19, 12,  5, 48, 41, 34,
    27, 20, 13,  6, 56, 49, 42, 35,
    28, 21, 14,  7, 57, 50, 43, 36,
    29, 22, 15, 58, 51, 44, 37, 30,
    23, 59, 52, 45, 38, 31, 60, 53,
    46, 39, 61, 54, 47, 62, 55, 63
};
__constant uchar g_lastCoeffTable[32] =
{
    0x00, 0x01, 0x02, 0x03, 0x14, 0x14, 0x15, 0x15,
    0x26, 0x26, 0x26, 0x26, 0x27, 0x27, 0x27, 0x27,
    0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38,
    0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39, 0x39,
};
__constant ushort coeff_scan4[4][16] =
{
    { 0,  4,  1,  8,  5,  2, 12,  9,  6,  3, 13, 10,  7, 14, 11, 15 },
    { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 0,  4,  8, 12,  1,  5,  9, 13,  2,  6, 10, 14,  3,  7, 11, 15 },
    { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }
};

__constant uchar sig_context[5][16] = {
    {2,1,1,0,1,1,0,0,1,0,0,0,0,0,0,0},
    {2,2,2,2,1,1,1,1,0,0,0,0,0,0,0,0},
    {2,1,0,0,2,1,0,0,2,1,0,0,2,1,0,0},
    {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
    {0,1,4,5,2,3,4,5,6,6,8,8,7,7,8,8}
};
// Offsets from the pinned x265 common/contexts.h.
#define CF_SIG_CG 39u
#define CF_SIG 43u
#define CF_LAST 85u
#define CF_ONE 121u
#define CF_ABS 145u
uint cf_group(uint lg,uint scan,uint i) {
    if (lg == 2) return 0;
    if (lg == 3) return g_scan2x2[scan == 1][i];
    if (lg == 4) return coeff_scan4[0][i];
    return g_scan8x8diag[i];
}
uint cf_position(uint lg,uint scan,uint i) {
    uint cg = cf_group(lg,scan,i/16), sub = coeff_scan4[scan][i%16], side=1u<<(lg-2);
    return ((cg/side)*4+sub/4)*(1u<<lg)+(cg%side)*4+sub%4;
}
typedef struct {
    __global uint4* ops;
    __global uint* errors;
    uint offset, count, capacity, emit;
} CoeffWriter;
void cf_op(__private CoeffWriter* w,uint kind,uint value,uint argument) {
    if (w->emit) {
        uint i=w->offset+w->count;
        if(i<w->capacity) w->ops[i]=(uint4)(kind,value,argument,0);
        else atomic_or(w->errors,1u);
    }
    w->count++;
}
void cf_bin(__private CoeffWriter* w,uint value,uint context) { cf_op(w,0,value,context); }
void cf_ep(__private CoeffWriter* w,uint value,uint bits) { cf_op(w,1,value,bits); }
void cf_rice(__private CoeffWriter* w,uint number,uint rice) {
    uint remainder=number&((1u<<rice)-1);
    uint prefix=number>>rice;
    if(prefix<3) cf_ep(w,(((1u<<(prefix+1))-2)<<rice)+remainder,prefix+1+rice);
    else {
        number=prefix-3;
        uint length=31-clz(number+1);
        number-=(1u<<length)-1;
        cf_ep(w,(1u<<(4+length))-2,4+length);
        cf_ep(w,(number<<rice)+remainder,length+rice);
    }
}
// One workgroup resolves a TU's nonzero group mask, last scan position and
// each group's final greater-than-one state. These states depend on coefficients,
// not on the incoming CABAC probability state.
__attribute__((reqd_work_group_size(64,1,1)))
__kernel void coefficient_metadata(__global const short* coefficients,
                                  __global const uint4* parameters,uint blocks,uint lg,
                                  __global ulong* masks,__global int* lasts,
                                  __global uint* previous_states,__global uint* errors) {
    uint block=get_group_id(0),lane=get_local_id(0);
    if(block>=blocks) return;
    uint4 p=parameters[block];uint luma=p.x,scan=p.y;
    if(lg<2 || lg>5 || luma>1 || scan>2 || (scan && (lg>3 || (!luma && lg>2))) || p.z || p.w) {
        if(!lane) atomic_or(errors,2u);
        return;
    }
    uint area=1u<<(2*lg),groups=area/16;
    __local ulong flags[64]; __local int last[64];
    ulong mask=0; int found=-1; uint nonzero=0,greater=0;
    if(lane<groups) {
        for(int j=15;j>=0;j--) {
            uint at=lane*16+(uint)j;
            int value=coefficients[block*area+cf_position(lg,scan,at)];
            if(value) {
                if(found<0) found=(int)at;
                if(nonzero<8 && abs(value)>1) greater=1;
                nonzero++;
            }
        }
        if(nonzero) mask=(ulong)1<<cf_group(lg,scan,lane);
        previous_states[block*groups+lane]=greater;
    }
    flags[lane]=mask; last[lane]=found;
    barrier(CLK_LOCAL_MEM_FENCE);
    for(uint step=32;step;step/=2) {
        if(lane<step) {flags[lane]|=flags[lane+step];last[lane]=max(last[lane],last[lane+step]);}
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(!lane) {masks[block]=flags[0];lasts[block]=last[0];}
}
// Two passes: count bins, then write at GPU-computed offsets. Each work item
// codes one 4x4 coefficient group, retaining reverse group order within a TU.
__kernel void coefficient_bins(__global const short* coefficients,
                                __global const uint4* parameters,
                                uint blocks,uint lg,uint emit,
                                __global const uint* offsets,
                                __global uint* counts,__global uint4* ops,
                                uint capacity,__global uint* errors,
                                __global const ulong* masks,__global const int* lasts,
                                __global const uint* previous_states) {
    if(lg<2 || lg>5) { if(!get_global_id(0)) atomic_or(errors,2u); return; }
    uint groupCount=1u<<(2*lg-4),item=get_global_id(0),block=item/groupCount;
    if(block>=blocks) return;
    uint4 p=parameters[block]; uint luma=p.x,scan=p.y;
    if(lg<2 || lg>5 || luma>1 || scan>2 || (scan && (lg>3 || (!luma && lg>2))) || p.z || p.w) {
        counts[item]=0; atomic_or(errors,2u); return;
    }
    uint n=1u<<lg, area=n*n,side=n/4;
    __global const short* coeff=coefficients+block*area;
    CoeffWriter w={ops,errors,emit?offsets[item]:0,0,capacity,emit};
    int last=lasts[block],subset=(int)(groupCount-1-item%groupCount);
    ulong groups=masks[block];
    if(last<0 || subset>last/16) { counts[item]=0; return; }
    if(subset==last/16) {
    uint lastPosition=cf_position(lg,scan,(uint)last);
    uint positions[2]={lastPosition%n,lastPosition/n};
    if(scan==2) { uint t=positions[0];positions[0]=positions[1];positions[1]=t; }
    uint suffix=0,suffixBits=0;
    uint ctxStart=luma?3*(lg-2)+(lg==5):15,ctxShift=luma?(lg>2):(lg-2),maximum=2*lg-1;
    for(uint k=0;k<2;k++) {
        uint packed=g_lastCoeffTable[positions[k]],ones=packed&15,bits=packed>>4;
        for(uint j=0;j<ones;j++) cf_bin(&w,1,CF_LAST+k*18+ctxStart+(j>>ctxShift));
        if(ones<maximum) cf_bin(&w,0,CF_LAST+k*18+ctxStart+(ones>>ctxShift));
        suffix=(suffix<<bits)|(positions[k]&((1u<<bits)-1)); suffixBits+=bits;
    }
    cf_ep(&w,suffix,suffixBits);
    }
    uint c1=1;
    for(int prior=subset+1;prior<=last/16;prior++) {
        if((groups>>cf_group(lg,scan,(uint)prior))&1) {
            c1=previous_states[block*groupCount+(uint)prior]?0:1;break;
        }
    }
    {
        uint cg=cf_group(lg,scan,(uint)subset),x=cg%side,y=cg/side;
        uint right=x+1<side?(uint)((groups>>(cg+1))&1):0;
        uint lower=y+1<side?(uint)((groups>>(cg+side))&1):0;
        uint present=(uint)((groups>>cg)&1);
        if(subset==last/16 || subset==0) { groups|=(ulong)1<<cg; present=1; }
        else cf_bin(&w,present,CF_SIG_CG+(luma?0:2)+(right|lower));
        uint absCoeff[16],nonzero=0,signs=0;
        int start=15;
        if(subset==last/16) {
            int value=coeff[cf_position(lg,scan,(uint)last)]; absCoeff[nonzero++]=abs(value); signs=value<0;
            start=last%16-1;
        }
        if(present) for(int j=start;j>=0;j--) {
            uint i=(uint)subset*16+(uint)j, pos=cf_position(lg,scan,i), sub=coeff_scan4[scan][j];
            int value=coeff[pos]; uint significant=value!=0;
            if(lg==2 || j!=0 || subset==0 || nonzero) {
                uint context;
                if(lg==2) context=sig_context[4][sub];
                else {
                    uint base=lg==3?((scan && luma)?15:9):(luma?21:12);
                    context=i?(sig_context[right+2*lower][sub]+base+((luma && subset)?3:0)):0;
                }
                cf_bin(&w,significant,CF_SIG+(luma?0:27)+context);
            }
            if(significant) { absCoeff[nonzero++]=abs(value); signs=(signs<<1)|(value<0); }
        }
        if(!nonzero) { counts[item]=w.count; return; }
        uint set=((subset>0 && luma)?2:0)+((c1&3)==0);
        uint firstTwo=8,firstFlag=2; c1=1;
        for(uint i=0;i<min(nonzero,8u);i++) {
            uint greater=absCoeff[i]>1;
            cf_bin(&w,greater,CF_ONE+(luma?0:16)+4*set+c1);
            if(greater && firstTwo==8) { firstTwo=i; firstFlag=absCoeff[i]>2; }
            c1=greater?0:(c1?min(c1+1,3u):0);
        }
        if(!c1) cf_bin(&w,firstFlag,CF_ABS+(luma?0:4)+set);
        cf_ep(&w,signs,nonzero);
        if(!c1 || nonzero>8) {
            uint rice=0,base=3;
            for(uint i=firstTwo;i<nonzero;i++) {
                if(i>=8) base=1;
                if(absCoeff[i]>=base) {
                    cf_rice(&w,absCoeff[i]-base,rice);
                    if(absCoeff[i]>(3u<<rice) && rice<4) rice++;
                }
                base=2;
            }
        }
    }
    counts[item]=w.count;
}
// Convert the existing parallel scan's inclusive sums to exclusive bin offsets.
__kernel void coefficient_offsets(__global const uint* prefix,__global uint* offsets,
                                   uint blocks,__global uint* total) {
    uint i=get_global_id(0);
    if(i>=blocks) return;
    offsets[i]=i?prefix[i-1]:0;
    if(!i) *total=prefix[blocks-1]+1;
}
__kernel void coefficient_finish(__global const uint* total,__global uint4* ops,
                                  uint capacity,__global uint* errors) {
    if(get_global_id(0)) return;
    if(*total>capacity) { atomic_or(errors,1u); return; }
    ops[*total-1]=(uint4)(2,1,0,0);
}
