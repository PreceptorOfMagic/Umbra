// SPDX-License-Identifier: GPL-2.0-or-later
// Radius-one diamond traversal from pinned x265 encoder/motion.cpp:1016.
// SAD + pinned x265's QP28 MV costs. Search seeds are zero and prior-frame MV;
// this development subset does not reproduce x265's full candidate/RD search.
// Include after prediction.cl, compiled as 32x32 luma / CHROMA=0.
inline int me_inside(int ox,int oy,uint width,uint height,int2 v){
    int x=ox+v.x,y=oy+v.y;
    if(x<0 || y<0 || x+32>(int)width || y+32>(int)height)return 0;
    // An odd full-luma-pixel displacement is fractional in 4:2:0 chroma.
    // Keep its four-tap footprint inside the pane, not merely its MV origin.
    int cx=ox/2+(v.x>>1),cy=oy/2+(v.y>>1);
    if((v.x&1) && (cx<1 || cx+18>(int)width/2))return 0;
    if((v.y&1) && (cy<1 || cy+18>(int)height/2))return 0;
    return 1;
}
inline int me_fraction_inside(int ox,int oy,uint width,uint height,int2 v){
    int x=ox+(v.x>>2),y=oy+(v.y>>2);
    if(x<0 || y<0 || x+32>(int)width || y+32>(int)height)return 0;
    // Eight-tap luma and four-tap chroma must both stay inside this pane.
    if((v.x&3) && (x<3 || x+36>(int)width))return 0;
    if((v.y&3) && (y<3 || y+36>(int)height))return 0;
    int cx=ox/2+(v.x>>3),cy=oy/2+(v.y>>3);
    if((v.x&7) && (cx<1 || cx+18>(int)width/2))return 0;
    if((v.y&7) && (cy<1 || cy+18>(int)height/2))return 0;
    return 1;
}
inline int2 me_subpel_direction(uint c) {
    return c==0?(int2)(0,-1):c==1?(int2)(0,1):c==2?(int2)(-1,0):c==3?(int2)(1,0):
           c==4?(int2)(-1,-1):c==5?(int2)(1,-1):c==6?(int2)(-1,1):(int2)(1,1);
}
__kernel void motion_search(__global const ushort* input,__global const ushort* ref,__global const ushort* original,
    uint width,uint height,__global const int4* prior,__global int4* vectors,
    __global const ushort* mv_cost,uint limit) {
    uint block=get_group_id(0),lane=get_local_id(0),columns=width/32;
    if(block>=columns*(height/32))return;
    int ox=(block%columns)*32,oy=(block/columns)*32;
    uint candidate=lane/64,sub=lane%64;
    __local uint partial[256],best;
    __local int2 current;
    __local uint stop;
    __local short filtered[32*39];
    // A sampled wider seed can escape the local diamond minima of fine texture.
    // All proposed seeds are subsequently checked against every block pixel.
    __local ushort sparse[16];
    __local int2 trialVector[256],wide[2];
    int2 old=clamp(prior[block].zw>>2,(int2)(-(int)limit),(int2)((int)limit));
    if(!me_inside(ox,oy,width,height,old))old=(int2)(0);
    if(lane<16)sparse[lane]=input[(oy+(lane/4)*8+3)*width+ox+(lane%4)*8+3];
    barrier(CLK_LOCAL_MEM_FENCE);
    for(uint window=0;window<2;window++) {
        if(window && !any(old)) {if(!lane)wide[1]=wide[0];break;}
        int2 origin=window?old:(int2)(0),chosen=(int2)(0);
        uint score=0xffffffffu;
        for(uint index=lane;index<17*17;index+=256) {
            int2 trial=origin+(int2)((int)(index%17)-8,(int)(index/17)-8);
            if(any(abs(trial)>(uint2)(limit)) || !me_inside(ox,oy,width,height,trial))continue;
            uint value=0;
            for(uint i=0;i<16;i++)value+=abs((int)sparse[i]-(int)original[(oy+(int)(i/4)*8+3+trial.y)*width+ox+(int)(i%4)*8+3+trial.x]);
            value=value*64+mv_cost[abs(trial.x)*4]+mv_cost[abs(trial.y)*4];
            if(value<score){score=value;chosen=trial;}
        }
        partial[lane]=score;trialVector[lane]=chosen;barrier(CLK_LOCAL_MEM_FENCE);
        for(uint stride=128;stride;stride/=2) {
            if(lane<stride && partial[lane+stride]<partial[lane]){partial[lane]=partial[lane+stride];trialVector[lane]=trialVector[lane+stride];}
            barrier(CLK_LOCAL_MEM_FENCE);
        }
        if(!lane)wide[window]=trialVector[0];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    int2 seed=candidate==1?old:candidate>=2?wide[candidate-2]:(int2)(0);
    uint cost=0;
    for(uint i=sub;i<1024;i+=64)cost+=abs((int)input[(oy+(int)(i/32))*width+ox+i%32]-p_sample(ref,width,height,ox+i%32+seed.x,oy+i/32+seed.y));
    partial[lane]=cost;
    barrier(CLK_LOCAL_MEM_FENCE);
    for(uint step=32;step;step/=2){if(sub<step)partial[lane]+=partial[lane+step];barrier(CLK_LOCAL_MEM_FENCE);}
    if(!lane){
        current=(int2)(0);best=partial[0]+2*mv_cost[0];
        for(uint c=1;c<4;c++){
            int2 trial=c==1?old:wide[c-2];
            uint value=partial[c*64]+mv_cost[abs(trial.x)*4]+mv_cost[abs(trial.y)*4];
            if(value<best){best=value;current=trial;}
        }
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for(uint iteration=0;iteration<limit;iteration++){
        int2 delta=candidate==0?(int2)(0,-1):candidate==1?(int2)(0,1):candidate==2?(int2)(-1,0):(int2)(1,0);
        int2 trial=current+delta;cost=0;
        for(uint i=sub;i<1024;i+=64)cost+=abs((int)input[(oy+(int)(i/32))*width+ox+i%32]-p_sample(ref,width,height,ox+i%32+trial.x,oy+i/32+trial.y));
        partial[lane]=cost;
        barrier(CLK_LOCAL_MEM_FENCE);
        for(uint step=32;step;step/=2){if(sub<step)partial[lane]+=partial[lane+step];barrier(CLK_LOCAL_MEM_FENCE);}
        if(!lane){
            int2 chosen=current;stop=1;
            for(uint c=0;c<4;c++){
                int2 dir=c==0?(int2)(0,-1):c==1?(int2)(0,1):c==2?(int2)(-1,0):(int2)(1,0);
                int2 v=current+dir;
                if(abs(v.x)>limit || abs(v.y)>limit)continue;
                if(!me_inside(ox,oy,width,height,v))continue;
                uint value=partial[c*64]+mv_cost[abs(v.x)*4]+mv_cost[abs(v.y)*4];
                if(value<best){best=value;chosen=v;stop=0;}
            }
            current=chosen;
        }
        barrier(CLK_LOCAL_MEM_FENCE);
        if(stop)break;
    }
    // Fit a subpixel candidate to the final integer-neighbor costs, then
    // accept it only after full-block SAD using the exact HEVC interpolation.
    // A non-converged integer search supplies no valid neighboring cost fit.
    if(!lane) {
        int2 integer=current;
        current*=4;
        if(stop) {
            int centre=(int)best;
            int left=(int)partial[128]+mv_cost[abs(current.x-4)]+mv_cost[abs(current.y)];
            int right=(int)partial[192]+mv_cost[abs(current.x+4)]+mv_cost[abs(current.y)];
            int up=(int)partial[0]+mv_cost[abs(current.x)]+mv_cost[abs(current.y-4)];
            int down=(int)partial[64]+mv_cost[abs(current.x)]+mv_cost[abs(current.y+4)];
            int dx=0,dy=0,den=left+right-2*centre;
            if(den>0){int num=2*(left-right);dx=clamp((num+(num<0?-den/2:den/2))/den,-2,2);}
            den=up+down-2*centre;
            if(den>0){int num=2*(up-down);dy=clamp((num+(num<0?-den/2:den/2))/den,-2,2);}
            int2 trial=current+(int2)(dx,dy);
            if(abs(trial.x)<=limit*4 && abs(trial.y)<=limit*4 && me_fraction_inside(ox,oy,width,height,trial))
                integer=trial;
            else integer=current;
        } else integer=current;
        // Store the proposal separately; the integer winner stays in current.
        vectors[block]=(int4)(ox,oy,integer.x,integer.y);
    }
    barrier(CLK_LOCAL_MEM_FENCE|CLK_GLOBAL_MEM_FENCE);
    int2 trial=vectors[block].zw;
    if(any(trial!=current)) {
        int rx=ox+(trial.x>>2),ry=oy+(trial.y>>2),fx=trial.x&3,fy=trial.y&3;
        if(fx && fy)for(uint i=lane;i<32*39;i+=256) {
            int sum=0;
            for(int k=0;k<8;k++)sum+=p_luma[fx][k]*p_sample(ref,width,height,rx+i%32+k-3,ry+i/32-3);
            filtered[i]=(short)((sum-(8192<<(PIXEL_DEPTH-8)))>>(PIXEL_DEPTH-8));
        }
        barrier(CLK_LOCAL_MEM_FENCE);
        cost=0;
        for(uint i=lane;i<1024;i+=256) {
            int x=i%32,y=i/32,value=0;
            if(fx && fy) {
                for(int k=0;k<8;k++)value+=p_luma[fy][k]*filtered[(y+k)*32+x];
                const int shift=6+14-PIXEL_DEPTH;
                value=(value+(1<<(shift-1))+(8192<<6))>>shift;
            } else if(fx || fy) {
                for(int k=0;k<8;k++)value+=p_luma[fx?fx:fy][k]*p_sample(ref,width,height,rx+x+(fx?k-3:0),ry+y+(fy?k-3:0));
                value=(value+32)>>6;
            } else value=p_sample(ref,width,height,rx+x,ry+y);
            cost+=abs((int)input[(oy+y)*width+ox+x]-clamp(value,0,(1<<PIXEL_DEPTH)-1));
        }
        partial[lane]=cost;barrier(CLK_LOCAL_MEM_FENCE);
        for(uint stride=128;stride;stride/=2){if(lane<stride)partial[lane]+=partial[lane+stride];barrier(CLK_LOCAL_MEM_FENCE);}
        if(!lane && partial[0]+mv_cost[abs(trial.x)]+mv_cost[abs(trial.y)]<best)current=trial;
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(!lane)vectors[block]=(int4)(ox,oy,current.x,current.y);
}
__kernel void chroma_vectors(__global const int4* luma,__global int4* chroma,uint count){
    uint i=get_global_id(0);if(i<count){int4 v=luma[i];chroma[i]=(int4)(v.x/2,v.y/2,v.z,v.w);}
}
