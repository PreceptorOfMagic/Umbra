// SPDX-License-Identifier: GPL-2.0-or-later
// Calls pinned upstream x265 C interpolation primitives; CPU test oracle only.
#include "common.h"
#include "primitives.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace X265_NS;
static void put(FILE* f,const void* p,size_t n) {
    if (fwrite(p,1,n,f)!=n) { perror("write"); exit(1); }
}
static unsigned random32(unsigned& s) { s^=s<<13; s^=s>>17; s^=s<<5; return s; }
static void make(const char* dir,unsigned lg,bool chroma,unsigned pattern,unsigned orientation) {
    const unsigned n=1u<<lg, phases=chroma?8:4, max=(1u<<X265_DEPTH)-1;
    const unsigned width=orientation?(orientation==1?3840:1920)>>(chroma?1:0):96;
    const unsigned height=orientation?(orientation==1?1088:2176)>>(chroma?1:0):80;
    const unsigned count=orientation?width*height/(n*n):phases*phases*8;
    std::vector<pixel> ref(size_t(width)*height), padded(size_t(width+64)*(height+64));
    std::vector<uint16_t> expanded(ref.size()), expected(size_t(count)*n*n);
    std::vector<int32_t> blocks(size_t(count)*4);
    unsigned seed=0x286fc217;
    for (unsigned y=0;y<height;y++) for (unsigned x=0;x<width;x++) {
        unsigned v=random32(seed)&max;
        if (pattern==0) v=0;
        if (pattern==1) v=max;
        if (pattern==2) v=((x+y)&1)?max:0;
        if (pattern==3) v=(x*17+y*37)&max;
        if (pattern==4) v=(x==width/2 && y==height/2)?max:0;
        ref[size_t(y)*width+x]=pixel(v); expanded[size_t(y)*width+x]=uint16_t(v);
    }
    const int stride=int(width)+64;
    for (int y=-32;y<int(height)+32;y++) for (int x=-32;x<int(width)+32;x++)
        padded[size_t(y+32)*stride+x+32]=ref[size_t(std::max(0,std::min(y,int(height)-1)))*width+std::max(0,std::min(x,int(width)-1))];
    const int part=partitionFromSizes(int(chroma?n*2:n),int(chroma?n*2:n));
    std::vector<pixel> out(n*n);
    std::vector<int16_t> intermediate(n*(n+7));
    for (unsigned b=0;b<count;b++) {
        unsigned fx=b%phases, fy=(b/phases)%phases, pos=(b/(phases*phases))%8;
        int x=orientation?int((b%(width/n))*n):int(pos&1?width-n:0);
        int y=orientation?int((b/(width/n))*n):int(pos&2?height-n:0);
        // Both signs and every fractional phase, including motion across picture edges.
        int dx=(pos&4)?-12:11, dy=(pos&4)?-10:9;
        if (!orientation && pattern==4) { x=int(width/2); y=int(height/2); dx=-1; dy=-1; }
        blocks[b*4]=x; blocks[b*4+1]=y;
        blocks[b*4+2]=dx*int(phases)+int(fx); blocks[b*4+3]=dy*int(phases)+int(fy);
        const pixel* src=padded.data()+size_t(y+dy+32)*stride+x+dx+32;
        if (!(fx|fy)) {
            for (unsigned row=0;row<n;row++) std::copy_n(src+row*stride,n,out.data()+row*n);
        } else if (!chroma) {
            if (!fy) primitives.pu[part].luma_hpp(src,stride,out.data(),n,int(fx));
            else if (!fx) primitives.pu[part].luma_vpp(src,stride,out.data(),n,int(fy));
            else primitives.pu[part].luma_hvpp(src,stride,out.data(),n,int(fx),int(fy));
        } else {
            auto& p=primitives.chroma[X265_CSP_I420].pu[part];
            if (!fy) p.filter_hpp(src,stride,out.data(),n,int(fx));
            else if (!fx) p.filter_vpp(src,stride,out.data(),n,int(fy));
            else {
                p.filter_hps(src,stride,intermediate.data(),n,int(fx),1);
                p.filter_vsp(intermediate.data()+n,n,out.data(),n,int(fy));
            }
        }
        for (unsigned i=0;i<n*n;i++) expected[size_t(b)*n*n+i]=out[i];
    }
    char name[4096];
    snprintf(name,sizeof(name),"%s/%s-d%d-%c%u-p%u-o%u.prediction",dir,orientation?"native":"edges",X265_DEPTH,chroma?'c':'y',n,pattern,orientation);
    FILE* f=fopen(name,"wb"); if (!f) { perror(name); exit(1); }
    put(f,"AXPRED01",8);
    uint32_t head[]={X265_DEPTH,lg,chroma?1u:0u,width,height,count}; put(f,head,sizeof(head));
    put(f,expanded.data(),expanded.size()*2); put(f,blocks.data(),blocks.size()*4); put(f,expected.data(),expected.size()*2);
    if (fclose(f)) { perror("close"); exit(1); }
    printf("fixture=%s blocks=%u samples=%zu\n",name,count,expected.size());
}
int main(int argc,char** argv) {
    if (argc!=2) { fprintf(stderr,"Usage: prediction_oracle existing-output-directory\n"); return 2; }
    setupCPrimitives(primitives);
    for (unsigned lg=2;lg<=5;lg++) for (unsigned c=0;c<=1;c++) {
        if (c && lg==5) continue; // largest intended CTB is 32 luma / 16 chroma
        for (unsigned p=0;p<6;p++) make(argv[1],lg,c!=0,p,0);
    }
    for (unsigned o=1;o<=2;o++) for (unsigned c=0;c<=1;c++) make(argv[1],c?3:4,c!=0,5,o);
    return 0;
}
