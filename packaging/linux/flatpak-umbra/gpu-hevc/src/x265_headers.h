// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Pinned x265 supplies configuration syntax only. No x265_encoder_encode calls.
#include "common.h"
#include "encoder.h"
#include "entropy.h"
#include "framedata.h"
#include "slice.h"
#include "bitcost.h"
#include <array>
#include <vector>
#include <stdexcept>
namespace aurora_headers {
using namespace X265_NS;
struct SliceHeader {
    uint32_t nalType;
    std::vector<uint8_t> bytes;
    std::array<uint8_t,160> contexts;
};
class Writer {
    x265_param* param=nullptr;
    x265_encoder* handle=nullptr;
    void close(){if(handle)x265_encoder_close(handle);if(param)x265_param_free(param);handle=nullptr;param=nullptr;}
public:
    std::vector<uint8_t> parameterSets;
    std::vector<uint16_t> mvCosts;
    Writer(uint32_t width,uint32_t height,uint32_t depth,bool hdr,bool adaptiveQp=false,bool deblock=false,bool isolatedIntra=false) {
        if(depth!=X265_DEPTH || width<64 || height<64 || width>8192 || height>8192 || width%32 || height%32 || (hdr && depth!=10))
            throw std::runtime_error("Unsupported x265 header configuration");
        try {
            param=x265_param_alloc();if(!param)throw std::runtime_error("x265 parameter allocation failed");
            if(x265_param_default_preset(param,"ultrafast","zerolatency"))throw std::runtime_error("x265 preset unavailable");
            param->sourceWidth=width;param->sourceHeight=height;param->internalBitDepth=X265_DEPTH;param->internalCsp=X265_CSP_I420;
            // Shared syntax timing, not a pacing loop or a limit on submissions.
            param->fpsNum=120;param->fpsDenom=1;param->maxCUSize=32;param->minCUSize=16;param->maxTUSize=32;
            param->tuQTMaxInterDepth=1;param->tuQTMaxIntraDepth=1;param->bEnableWavefront=0;param->frameNumThreads=1;memcpy(param->numaPools,"none",5);
            param->bframes=0;param->maxNumReferences=1;param->bEnableTemporalMvp=0;param->bEnableWeightedPred=0;
            // Isolated P CUs predict from already reconstructed inter neighbors.
            // Their selection changes GPU syntax, not this unconstrained PPS flag.
            (void)isolatedIntra;
            param->bEnableConstrainedIntra=0;param->bEnableLoopFilter=deblock?1:0;param->bEnableSAO=0;param->selectiveSAO=0;param->bEnableSignHiding=0;
            param->bEmitInfoSEI=0;param->bRepeatHeaders=0;param->bEnableAccessUnitDelimiters=0;param->bOptQpPPS=0;
            param->bOptRefListLengthPPS=0;param->rc.aqMode=0;param->rc.cuTree=0;param->rc.rateControlMode=X265_RC_CQP;param->rc.qp=28;
            // x265 disables DQP in CQP mode. CRF/AQ here initializes syntax
            // only; our GPU supplies every picture and its per-CTU QP.
            if(adaptiveQp){param->rc.rateControlMode=X265_RC_CRF;param->rc.aqMode=X265_AQ_VARIANCE;param->rc.aqStrength=1;param->rc.qgSize=32;}
            param->logLevel=X265_LOG_WARNING;
            param->vui.bEnableVideoSignalTypePresentFlag=1;param->vui.videoFormat=5;
            param->vui.bEnableVideoFullRangeFlag=0;param->vui.bEnableColorDescriptionPresentFlag=1;
            param->vui.colorPrimaries=hdr?9:1;param->vui.transferCharacteristics=hdr?16:1;param->vui.matrixCoeffs=hdr?9:1;
            handle=x265_encoder_open(param);if(!handle)throw std::runtime_error("x265 header initialization failed");
            if(adaptiveQp && (!static_cast<Encoder*>(handle)->m_pps.bUseDQP || static_cast<Encoder*>(handle)->m_pps.maxCuDQPDepth!=0))
                throw std::runtime_error("x265 did not enable one quantization group per CTU");
            x265_nal* nals=nullptr;uint32_t count=0;
            if(x265_encoder_headers(handle,&nals,&count)<0)throw std::runtime_error("x265 parameter sets failed");
            for(uint32_t i=0;i<count;i++)parameterSets.insert(parameterSets.end(),nals[i].payload,nals[i].payload+nals[i].sizeBytes);
            BitCost costs;costs.setQP(28);costs.setMVP(MV(0,0));uint16_t zero=costs.mvcost(MV(0,0))/2;
            mvCosts.resize(257);for(int i=0;i<=256;i++)mvCosts[i]=uint16_t(costs.mvcost(MV(i,0))-zero);
        }catch(...){close();throw;}
    }
    ~Writer(){close();}
    Writer(const Writer&)=delete;
    SliceHeader slice(uint32_t poc,bool intra,uint32_t qp=28) {
        if(qp>51)throw std::runtime_error("Slice QP outside 0..51");
        auto e=static_cast<Encoder*>(handle);
        Slice s;s.m_param=e->m_param;s.m_sps=&e->m_sps;s.m_pps=&e->m_pps;
        // Only the SPS's POC LSB field is serialized. Maintain a separate 64-bit
        // session frame identity in the caller; no fixed-length header replay.
        s.m_poc=int(poc&((1u<<e->m_sps.log2MaxPocLsb)-1));s.m_sliceQp=int(qp);s.m_sliceType=intra?I_SLICE:P_SLICE;
        s.m_nalUnitType=intra?NAL_UNIT_CODED_SLICE_IDR_N_LP:NAL_UNIT_CODED_SLICE_TRAIL_R;
        s.m_sLFaseFlag=false;s.m_maxNumMergeCand=1;s.m_bUseSao=0;s.m_rpsIdx=-1;s.m_iNumRPSInSPS=0;
        s.m_numRefIdx[0]=intra?0:1;s.m_numRefIdx[1]=0;s.m_rps=RPS();
        if(!intra){s.m_rps.numberOfPictures=1;s.m_rps.numberOfNegativePictures=1;s.m_rps.deltaPOC[0]=-1;s.m_rps.bUsed[0]=true;}
        FrameData data;data.m_param=e->m_param;Bitstream bits;Entropy coder;coder.setBitstream(&bits);
        coder.codeSliceHeader(s,data,0,0,int(qp),0);bits.writeByteAlignment();
        SliceHeader out;out.nalType=s.m_nalUnitType;out.bytes.assign(bits.getFIFO(),bits.getFIFO()+bits.getNumberOfWrittenBytes());
        memset(coder.m_contextState,0,sizeof(coder.m_contextState));coder.resetEntropy(s);memcpy(out.contexts.data(),coder.m_contextState,out.contexts.size());
        return out;
    }
};
}
