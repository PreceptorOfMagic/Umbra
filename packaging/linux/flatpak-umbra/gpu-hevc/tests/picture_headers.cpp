// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse pinned x265 for parameter sets, slice headers and initial contexts only.
// No picture is submitted to x265 and no CPU pixel encoder is used here.
#include "common.h"
#include "encoder.h"
#include "entropy.h"
#include "framedata.h"
#include "slice.h"
#include "bitcost.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace X265_NS;
static void put(FILE* f,const void* p,size_t n) { if(fwrite(p,1,n,f)!=n) abort(); }
static void word(FILE* f,uint32_t n) { put(f,&n,4); }
int main(int argc,char** argv) {
    if(argc<5 || argc>7) { fprintf(stderr,"Usage: picture_headers output width height frames [sdr|hdr [reset-frame]]\n");return 2; }
    int width=atoi(argv[2]),height=atoi(argv[3]),frames=atoi(argv[4]);
    if(width<64 || height<64 || width%32 || height%32 || frames<2 || frames>1024) return 2;
    bool hdr=argc>=6 && !strcmp(argv[5],"hdr");
    if(argc>=6 && strcmp(argv[5],"sdr") && !hdr) return 2;
    if(hdr && X265_DEPTH!=10)return 2;
    int reset=argc==7?atoi(argv[6]):0;
    if(reset<0 || reset>=frames)return 2;
    x265_param* p=x265_param_alloc(); x265_param_default_preset(p,"ultrafast","zerolatency");
    p->sourceWidth=width;p->sourceHeight=height;p->internalBitDepth=X265_DEPTH;p->internalCsp=X265_CSP_I420;
    p->fpsNum=120;p->fpsDenom=1;p->maxCUSize=32;p->minCUSize=16;p->maxTUSize=32;
    p->tuQTMaxInterDepth=1;p->tuQTMaxIntraDepth=1;p->bEnableWavefront=0;p->frameNumThreads=1;memcpy(p->numaPools,"none",5);
    p->bframes=0;p->maxNumReferences=1;p->bEnableTemporalMvp=0;p->bEnableWeightedPred=0;
    p->bEnableLoopFilter=0;p->bEnableSAO=0;p->selectiveSAO=0;p->bEnableSignHiding=0;
    p->bEmitInfoSEI=0;p->bRepeatHeaders=0;p->bEnableAccessUnitDelimiters=0;p->bOptQpPPS=0;
    p->bOptRefListLengthPPS=0;p->rc.aqMode=0;p->rc.cuTree=0;p->rc.rateControlMode=X265_RC_CQP;p->rc.qp=28;
    p->logLevel=X265_LOG_WARNING;
    if(argc>=6) {
        p->vui.bEnableVideoSignalTypePresentFlag=1;p->vui.videoFormat=5;
        p->vui.bEnableVideoFullRangeFlag=0;p->vui.bEnableColorDescriptionPresentFlag=1;
        p->vui.colorPrimaries=hdr?9:1;p->vui.transferCharacteristics=hdr?16:1;p->vui.matrixCoeffs=hdr?9:1;
    }
    x265_encoder* enc=x265_encoder_open(p); if(!enc) return 1;
    Encoder* e=static_cast<Encoder*>(enc);
    x265_nal* nals=nullptr;uint32_t count=0;
    if(x265_encoder_headers(enc,&nals,&count)<0) return 1;
    FILE* f=fopen(argv[1],"wb");if(!f) return 1;
    put(f,"AXHEAD02",8);word(f,width);word(f,height);word(f,X265_DEPTH);word(f,28);word(f,frames);
    uint32_t bytes=0;for(uint32_t i=0;i<count;i++)bytes+=nals[i].sizeBytes;
    word(f,bytes);for(uint32_t i=0;i<count;i++)put(f,nals[i].payload,nals[i].sizeBytes);
    // Configuration table only; no frame analysis is performed by this program.
    BitCost costs;costs.setQP(28);costs.setMVP(MV(0,0));
    uint16_t zero=costs.mvcost(MV(0,0))/2,table[257];
    for(int i=0;i<=256;i++)table[i]=uint16_t(costs.mvcost(MV(i,0))-zero);
    put(f,table,sizeof(table));
    for(int frame=0;frame<frames;frame++) {
        Slice s;s.m_param=e->m_param;s.m_sps=&e->m_sps;s.m_pps=&e->m_pps;
        bool intra=frame==0 || (reset && frame==reset);
        s.m_poc=(reset && frame>=reset?frame-reset:frame)&((1u<<e->m_sps.log2MaxPocLsb)-1);s.m_sliceQp=28;s.m_sliceType=intra?I_SLICE:P_SLICE;
        s.m_nalUnitType=intra?NAL_UNIT_CODED_SLICE_IDR_N_LP:NAL_UNIT_CODED_SLICE_TRAIL_R;
        s.m_maxNumMergeCand=1;s.m_bUseSao=0;s.m_rpsIdx=-1;s.m_iNumRPSInSPS=0;
        s.m_numRefIdx[0]=intra?0:1;s.m_numRefIdx[1]=0;
        memset(&s.m_rps,0,sizeof(s.m_rps));
        if(!intra) {s.m_rps.numberOfPictures=1;s.m_rps.numberOfNegativePictures=1;s.m_rps.deltaPOC[0]=-1;s.m_rps.bUsed[0]=true;}
        FrameData data;data.m_param=e->m_param;
        Bitstream bits;Entropy coder;coder.setBitstream(&bits);
        coder.codeSliceHeader(s,data,0,0,28,0);bits.writeByteAlignment();
        word(f,s.m_nalUnitType);word(f,bits.getNumberOfWrittenBytes());put(f,bits.getFIFO(),bits.getNumberOfWrittenBytes());
        memset(coder.m_contextState,0,sizeof(coder.m_contextState));coder.resetEntropy(s);
        put(f,coder.m_contextState,160);
    }
    if(fclose(f))return 1;
    printf("headers_only width=%d height=%d depth=%d frames=%d CTB=%u minCB=%u TUdepth=%u/%u WPP=%d\n",width,height,X265_DEPTH,frames,
        e->m_param->maxCUSize,e->m_param->minCUSize,e->m_param->tuQTMaxInterDepth,e->m_param->tuQTMaxIntraDepth,e->m_param->bEnableWavefront);
    x265_encoder_close(enc);x265_param_free(p);return 0;
}
