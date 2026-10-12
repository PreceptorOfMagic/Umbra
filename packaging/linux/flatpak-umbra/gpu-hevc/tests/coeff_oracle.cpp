// SPDX-License-Identifier: GPL-2.0-or-later
// Pinned x265 supplies independent syntax and CABAC reference output.
#include "common.h"
#include "primitives.h"
#include "entropy.h"
#include "cudata.h"
#include "slice.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
namespace X265_NS {
struct AuroraCabacOracle {
    static uint32_t random(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    static void write(FILE* f,const void* p,size_t n) { if(fwrite(p,1,n,f)!=n) abort(); }
    static void run(unsigned id,unsigned lg,unsigned luma,unsigned scan,const char* dir) {
        char name[4096]; snprintf(name,sizeof(name),"%s/slice-%04u.coeff",dir,id);
        FILE* f=fopen(name,"wb"); if(!f) abort();
        unsigned area=1u<<(2*lg), blocks=64;
        write(f,"AXCOEF01",8); uint32_t header[]={lg,blocks}; write(f,header,sizeof(header));
        PPS pps={}; Slice slice; slice.m_pps=&pps; slice.m_sliceType=P_SLICE; slice.m_sliceQp=28;
        CUData cu;
        uint8_t mode=scan?MODE_INTRA:MODE_INTER, dirMode=scan==1?26:10, zero=0;
        cu.m_slice=&slice; cu.m_predMode=&mode; cu.m_tqBypass=&zero;
        cu.m_lumaIntraDir=&dirMode; cu.m_chromaIntraDir=&dirMode;
        cu.m_hChromaShift=1; cu.m_chromaFormat=X265_CSP_I420;
        Bitstream bits; Entropy coder; coder.setBitstream(&bits); coder.resetEntropy(slice);
        uint32_t seed=0x21736abd+id;
        alignas(32) int16_t coeff[1024];
        for(unsigned b=0;b<blocks;b++) {
            for(unsigned i=0;i<area;i++) {
                int value=int(random(seed)%4096)-2048;
                switch(b%8) {
                case 0: value=0; break;
                case 1: value=i==0?1:0; break;
                case 2: value=i+1==area?-32768:0; break;
                case 3: value=i&1?1:-1; break;
                case 4: if(random(seed)%7) value=0; break;
                case 5: value=int(random(seed)%7)-3; break;
                case 6: value=i==area/2?32767:0; break;
                default: break;
                }
                coeff[i]=int16_t(value);
            }
            uint32_t meta[]={luma,scan,0,0}; write(f,meta,sizeof(meta)); write(f,coeff,area*2);
            if(primitives.cu[lg-2].count_nonzero(coeff))
                coder.codeCoeffNxN(cu,coeff,0,lg,luma?TEXT_LUMA:TEXT_CHROMA_U);
        }
        coder.finishSlice();
        if(fclose(f)) abort();
        printf("fixture=%s log2=%u luma=%u scan=%u blocks=%u\n",name,lg,luma,scan,blocks);
    }
    static void fromResidual(const char* dir,const char* input) {
        std::ifstream file(input,std::ios::binary|std::ios::ate);
        auto size=file.tellg(); if(size<24 || size>512*1024*1024) abort();
        std::vector<uint8_t> bytes(static_cast<size_t>(size)); file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),size) || memcmp(bytes.data(),"AXRES001",8)) abort();
        uint32_t header[4]; memcpy(header,bytes.data()+8,16);
        unsigned lg=header[1],blocks=header[2]; if(lg<2 || lg>5 || !blocks) abort();
        size_t area=size_t(1)<<(2*lg),stride=16+16*area;
        if(bytes.size()!=24+size_t(blocks)*stride) abort();
        char name[4096];snprintf(name,sizeof(name),"%s/slice-0000.coeff",dir);
        FILE* f=fopen(name,"wb");if(!f) abort();
        write(f,"AXCOEF01",8);uint32_t dimensions[]={lg,blocks};write(f,dimensions,8);
        PPS pps={};Slice slice;slice.m_pps=&pps;slice.m_sliceType=P_SLICE;slice.m_sliceQp=28;
        CUData cu;uint8_t mode=MODE_INTER,zero=0;cu.m_slice=&slice;cu.m_predMode=&mode;cu.m_tqBypass=&zero;
        cu.m_hChromaShift=1;cu.m_chromaFormat=X265_CSP_I420;
        Bitstream bits;Entropy coder;coder.setBitstream(&bits);coder.resetEntropy(slice);
        alignas(32) int16_t coeff[1024];
        for(unsigned b=0;b<blocks;b++) {
            memcpy(coeff,bytes.data()+24+size_t(b)*stride+16+8*area,area*2);
            uint32_t meta[]={1,0,0,0};write(f,meta,16);write(f,coeff,area*2);
            if(primitives.cu[lg-2].count_nonzero(coeff)) coder.codeCoeffNxN(cu,coeff,0,lg,TEXT_LUMA);
        }
        coder.finishSlice();if(fclose(f)) abort();
        printf("residual_source=%s depth=%u TU=%u blocks=%u\n",input,header[0],1u<<lg,blocks);
    }
};
}
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3) return 2;
    X265_NS::setupCPrimitives(X265_NS::primitives);
    if(argc==3) { X265_NS::AuroraCabacOracle::fromResidual(argv[1],argv[2]);return 0; }
    unsigned id=0;
    for(unsigned lg=2;lg<=5;lg++) for(unsigned luma=0;luma<2;luma++)
        for(unsigned scan=0;scan<((lg==2 || (lg==3 && luma))?3u:1u);scan++)
            X265_NS::AuroraCabacOracle::run(id++,lg,luma,scan,argv[1]);
    return 0;
}
