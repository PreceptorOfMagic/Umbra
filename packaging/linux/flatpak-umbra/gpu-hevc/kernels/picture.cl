// SPDX-License-Identifier: GPL-2.0-or-later
// Development picture syntax, adapted from x265 entropy.cpp encodeCU,
// encodeTransform, codePredInfo and finishCU at the pin in README.md.
// Contract: CTB32/minCB16; unsplit CU/TU; I=DC, P=explicit MV; one reference;
// Optional one-QG-per-CTU DQP; no SAO, deblock, sign hiding, WPP or weighted prediction.
// Include after coefficients.cl. This is a constrained codec proof, not a preset.
// Quarter-step picture quality uses a spatially balanced mix of neighboring
// integer CU QPs. It distributes quantization precision without suppressing
// high-frequency source detail. Phase rotates which CTUs receive the extra step.
__kernel void picture_quality_qp(__global const ushort* input,uint width,uint height,uint depth,uint base_qp,uint pressure,uint fraction,uint phase,
    __global uint* qps) {
    uint block=get_group_id(0),columns=width/32;
    if(get_local_id(0) || block>=columns*(height/32))return;
    uint rank=((block%columns&1)*2)^((block/columns&1)*3);
    uint extra=((rank+phase)&3)<fraction;
    qps[block]=min(base_qp+extra,51u);
}
// With QG=CTU32 and no WPP, x265 CUData::getRefQP/getLastCodedQP use
// the last coded QP (slice QP at the start). Zero-residual CTUs inherit it.
// A short GPU scan resolves that dependency without any pixel readback.
__kernel void picture_qp_deltas(uint blocks,uint depth,uint slice_qp,__global uint* qps,
    __global const int* ylast,__global const int* ulast,__global const int* vlast,__global int* deltas) {
    if(get_global_id(0))return;
    int previous=(int)slice_qp,bd=6*((int)depth-8);
    for(uint block=0;block<blocks;block++) {
        int delta=0;
        if(ylast[block]>=0 || ulast[block]>=0 || vlast[block]>=0) {
            int next=(int)qps[block];
            delta=(next-previous+78+bd+bd/2)%(52+bd)-26-bd/2;
            previous=next;
        }
        deltas[block]=delta;
        qps[block]=(uint)previous;
    }
}
// x265 entropy.cpp codeDeltaQP / writeUnaryMaxSymbol / writeEpExGolomb.
void pic_delta_qp(__private CoeffWriter* w,int delta) {
    uint magnitude=abs(delta),unary=min(magnitude,5u);
    cf_bin(w,unary!=0,16);
    if(unary){for(uint i=1;i<unary;i++)cf_bin(w,1,17);if(unary<5)cf_bin(w,0,17);}
    if(magnitude>=5) {
        uint symbol=magnitude-5,k=0,bits=0,n=0;
        while(symbol>=(1u<<k)){bits=bits*2+1;n++;symbol-=1u<<k;k++;}
        cf_ep(w,((bits*2)<<k)|symbol,n+1+k);
    }
    if(magnitude)cf_ep(w,delta<0,1);
}
inline uint pic_start(__global const uint* prefix,uint block,uint groups) {
    return block?prefix[block*groups-1]:0;
}
inline uint pic_length(__global const uint* prefix,uint block,uint groups) {
    return prefix[(block+1)*groups-1]-pic_start(prefix,block,groups);
}
void pic_copy(__private CoeffWriter* w,__global const uint4* src,uint first,uint length,uint emit) {
    if (emit) for (uint i=get_local_id(0);i<length;i+=get_local_size(0)) {
        if (w->offset+w->count+i<w->capacity) w->ops[w->offset+w->count+i]=src[first+i];
        else atomic_or(w->errors,1u);
    }
    w->count+=length;
}
void pic_eg(__private CoeffWriter* w,uint symbol){
    uint bins=0,n=0,k=1;
    while(symbol>=(1u<<k)){bins=2*bins+1;n++;symbol-=1u<<k;k++;}
    cf_ep(w,((bins*2)<<k)|symbol,n+1+k);
}
void pic_mvd(__private CoeffWriter* w,int2 delta){
    uint x=abs(delta.x),y=abs(delta.y);
    cf_bin(w,x!=0,26);cf_bin(w,y!=0,26);
    if(x)cf_bin(w,x>1,27);
    if(y)cf_bin(w,y>1,27);
    if(x){if(x>1)pic_eg(w,x-2);cf_ep(w,delta.x<0,1);}
    if(y){if(y>1)pic_eg(w,y-2);cf_ep(w,delta.y<0,1);}
}
__kernel void picture_bins(uint blocks,uint intra,uint emit,
    __global const uint* yprefix,__global const uint* uprefix,__global const uint* vprefix,
    __global const uint4* yops,__global const uint4* uops,__global const uint4* vops,
    __global const uint* offsets,__global uint* counts,__global uint4* ops,
    uint capacity,__global uint* errors,__global const int4* vectors,uint columns,
    __global const int* qp_deltas,uint adaptive_qp,__global const uint* intra_modes,uint isolated_intra) {
    uint block=get_group_id(0),lane=get_local_id(0); if(block>=blocks) return;
    CoeffWriter w={ops,errors,emit?offsets[block]:0,0,capacity,emit && !lane};
    uint yn=pic_length(yprefix,block,64),un=pic_length(uprefix,block,16),vn=pic_length(vprefix,block,16);
    uint any=(yn|un|vn)!=0;
    cf_bin(&w,0,0); // unsplit CTB32 (all neighboring CUs also depth zero)
    uint local_intra=intra || (isolated_intra && intra_modes[block]);
    if(!intra){cf_bin(&w,0,3);cf_bin(&w,local_intra,12);}
    if(local_intra) {
        // CU32 is larger than minCB16: intra part_mode is inferred 2Nx2N.
        cf_bin(&w,1,13); cf_ep(&w,2,2); // DC is MPM index 1: planar, DC, vertical
        cf_bin(&w,0,14); // chroma derives luma DC
    } else {
        cf_bin(&w,1,8); cf_bin(&w,0,6); // 2Nx2N, explicit motion
        // Whole-CTU 2Nx2N, all inter, same reference, TMVP disabled. Below-left
        // is not decoded yet; AMVP[0] is left, then above-right/above, then zero.
        // x265 common/cudata.cpp getPMV provides this candidate order.
        uint x=block%columns,y=block/columns;
        int2 predictor=(int2)(0);
        if(x && (!isolated_intra || !intra_modes[block-1]))predictor=vectors[block-1].zw;
        else if(y && x+1<columns && (!isolated_intra || !intra_modes[block-columns+1]))predictor=vectors[block-columns+1].zw;
        else if(y && (!isolated_intra || !intra_modes[block-columns]))predictor=vectors[block-columns].zw;
        else if(y && x && (!isolated_intra || !intra_modes[block-columns-1]))predictor=vectors[block-columns-1].zw;
        pic_mvd(&w,vectors[block].zw-predictor);cf_bin(&w,0,151);
        cf_bin(&w,any,38);
    }
    if(local_intra || any) {
        // max transform hierarchy depth one implies no TU subdivision flag.
        cf_bin(&w,un!=0,30); cf_bin(&w,vn!=0,30);
        if(local_intra || un || vn) cf_bin(&w,yn!=0,29);
        if(adaptive_qp && any)pic_delta_qp(&w,qp_deltas[block]);
        pic_copy(&w,yops,pic_start(yprefix,block,64),yn,emit);
        pic_copy(&w,uops,pic_start(uprefix,block,16),un,emit);
        pic_copy(&w,vops,pic_start(vprefix,block,16),vn,emit);
    }
    cf_op(&w,2,block+1==blocks,0); // end_of_slice_segment_flag
    if(!lane)counts[block]=w.count;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// x265 b81f650 common/deblock.cpp and loopfilter.cpp, adapted to unsplit
// CU32/TU32, one reference and no PCM/lossless blocks. Vertical then horizontal.
__constant uchar db_tc[54]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,5,5,6,6,7,8,9,10,11,13,14,16,18,20,22,24};
__constant uchar db_beta[52]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,7,8,9,10,11,12,13,14,15,16,17,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64};
inline int db_dp(__global const ushort* p,int d){return abs((int)p[-3*d]-2*(int)p[-2*d]+(int)p[-d]);}
inline int db_dq(__global const ushort* p,int d){return abs((int)p[0]-2*(int)p[d]+(int)p[2*d]);}
inline int db_strong(__global const ushort* p,int d,int beta,int tc){
    return abs((int)p[-4*d]-(int)p[-d])+abs((int)p[3*d]-(int)p[0])<(beta>>3) && abs((int)p[-d]-(int)p[0])<((5*tc+1)>>1);
}
__kernel void picture_deblock(__global ushort* pixels,uint width,uint height,uint depth,uint chroma,uint intra,uint axis,
    uint slice_qp,uint adaptive,__global const uint* qps,__global const int* ylast,__global const int4* vectors,__global const uint* intra_modes,uint isolated_intra) {
    uint side=32u>>chroma,columns=width/side,segments=side/4,id=get_global_id(0),block=id/segments;
    if(block>=columns*(height/side))return;
    uint bx=block%columns,by=block/columns;
    if((!axis && !bx) || (axis && !by))return; // Never cross a pane boundary.
    uint previous=block-(axis?columns:1),bs=2;
    if(!intra && !(isolated_intra && (intra_modes[block] || intra_modes[previous]))) {
        int2 delta=vectors[block].zw-vectors[previous].zw;
        bs=ylast[block]>=0 || ylast[previous]>=0 || any(abs(delta)>=(uint2)(4));
        if(!bs)return;
    }
    if(chroma && bs<=1)return;
    int qp=adaptive?(int)((qps[block]+qps[previous]+1)/2):(int)slice_qp;
    if(chroma && qp>=30){const int scale[22]={29,30,31,32,33,33,34,34,35,35,36,36,37,37,38,39,40,41,42,43,44,45};qp=scale[qp-30];}
    int tc=(int)db_tc[clamp(qp+2*((int)bs-1),0,53)]<<(depth-8),maximum=(1<<depth)-1;
    if(!tc)return;
    int step=axis?1:(int)width,offset=axis?(int)width:1;
    __global ushort* src=pixels+by*side*width+bx*side+(id%segments)*4*step;
    int strong=0,filterP1=0,filterQ1=0;
    if(!chroma) {
        int beta=(int)db_beta[clamp(qp,0,51)]<<(depth-8);
        int dp0=db_dp(src,offset),dq0=db_dq(src,offset),dp3=db_dp(src+3*step,offset),dq3=db_dq(src+3*step,offset);
        int d0=dp0+dq0,d3=dp3+dq3;
        if(d0+d3>=beta)return;
        strong=2*d0<(beta>>2) && 2*d3<(beta>>2) && db_strong(src,offset,beta,tc) && db_strong(src+3*step,offset,beta,tc);
        int sideThreshold=(beta+(beta>>1))>>3;
        filterP1=dp0+dp3<sideThreshold;filterQ1=dq0+dq3<sideThreshold;
    }
    for(int i=0;i<4;i++,src+=step) {
        int p0=src[-offset],p1=src[-2*offset],q0=src[0],q1=src[offset];
        if(chroma) {
            int delta=clamp((4*(q0-p0)+p1-q1+4)>>3,-tc,tc);
            src[-offset]=(ushort)clamp(p0+delta,0,maximum);src[0]=(ushort)clamp(q0-delta,0,maximum);
        }else if(strong) {
            int p2=src[-3*offset],p3=src[-4*offset],q2=src[2*offset],q3=src[3*offset],t=2*tc;
            src[-3*offset]=(ushort)(p2+clamp(((2*p3+3*p2+p1+p0+q0+4)>>3)-p2,-t,t));
            src[-2*offset]=(ushort)(p1+clamp(((p2+p1+p0+q0+2)>>2)-p1,-t,t));
            src[-offset]=(ushort)(p0+clamp(((p2+2*p1+2*p0+2*q0+q1+4)>>3)-p0,-t,t));
            src[0]=(ushort)(q0+clamp(((p1+2*p0+2*q0+2*q1+q2+4)>>3)-q0,-t,t));
            src[offset]=(ushort)(q1+clamp(((p0+q0+q1+q2+2)>>2)-q1,-t,t));
            src[2*offset]=(ushort)(q2+clamp(((p0+q0+q1+3*q2+2*q3+4)>>3)-q2,-t,t));
        }else {
            int delta=(9*(q0-p0)-3*(q1-p1)+8)>>4;
            if(abs(delta)<tc*10) {
                delta=clamp(delta,-tc,tc);
                src[-offset]=(ushort)clamp(p0+delta,0,maximum);src[0]=(ushort)clamp(q0-delta,0,maximum);
                if(filterP1)src[-2*offset]=(ushort)clamp(p1+clamp(((((int)src[-3*offset]+p0+1)>>1)-p1+delta)>>1,-(tc>>1),tc>>1),0,maximum);
                if(filterQ1)src[offset]=(ushort)clamp(q1+clamp(((((int)src[2*offset]+q0+1)>>1)-q1-delta)>>1,-(tc>>1),tc>>1),0,maximum);
            }
        }
    }
}

// Neighbor CUs have first been reconstructed as inter. This 2x2 grid leaves each
// selected CU's top/left neighbors inter-coded, so their reference samples remain
// immutable while selected CUs are refined in parallel. Intra is unconstrained.
// At pane borders, repeat the nearest available sample as x265 Predict does.
__kernel void picture_select_intra(__global const ushort* input,__global const ushort* predicted,
    __global const ushort* reconstructed,uint width,uint height,uint depth,uint intra,uint phase,__global uint* modes) {
    uint block=get_group_id(0),lane=get_local_id(0),columns=width/32;
    if(block>=columns*(height/32))return;
    uint bx=block%columns,by=block/columns;
    if(intra || bx%2!=phase%2 || by%2!=(phase/2)%2){if(!lane)modes[block]=intra;return;}
    __local uint inter[256],fresh[256],dc;uint a=0,b=0;
    uint origin=by*32*width+bx*32;
    if(!lane) {
        uint sum=32;
        for(uint k=0;k<32;k++) {
            uint top=by?reconstructed[origin-width+k]:(bx?reconstructed[origin-1]:(1u<<(depth-1)));
            uint left=bx?reconstructed[origin+k*width-1]:(by?reconstructed[origin-width]:(1u<<(depth-1)));
            sum+=top+left;
        }
        dc=sum/64;
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for(uint i=lane;i<1024;i+=256){
        int source=input[origin+(i/32)*width+i%32];
        int d=source-(int)predicted[block*1024+i],e=source-(int)dc;
        a+=(uint)(d*d);b+=(uint)(e*e);
    }
    inter[lane]=a;fresh[lane]=b;barrier(CLK_LOCAL_MEM_FENCE);
    for(uint step=128;step;step/=2){if(lane<step){inter[lane]+=inter[lane+step];fresh[lane]+=fresh[lane+step];}barrier(CLK_LOCAL_MEM_FENCE);}
    if(!lane)modes[block]=fresh[0]<inter[0];
}
