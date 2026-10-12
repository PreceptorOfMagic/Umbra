// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "picture_encoder.h"
#include "x265_headers.h"
#include "rate_control.h"
struct RateControlledPicture {
    aurora_headers::SliceHeader header;
    EncodedPicture picture;
    unsigned qp,passes;
    unsigned aqPressure=0;
    double targetBytes=0;unsigned quality=0;
};
// A large first picture/scene change can require another quantization pass.
// Scratch reconstruction is committed exactly once, so retries never change the
// reference used by this picture. There is no second decode or network hop.
static RateControlledPicture encodePicture(GpuPictureEncoder& encoder,aurora_headers::Writer& headers,
    PictureRateControl& rate,uint32_t poc,bool intra,double elapsed) {
    const double budget=rate.budget(elapsed,intra);
    unsigned quality=rate.start(intra),earlierQuality=0;size_t earlierBytes=0;
    for(unsigned pass=1;pass<=3;pass++) {
        const unsigned whole=quality/4,qp=std::min(51u,whole),aqPressure=whole-qp;
        auto header=headers.slice(poc,intra,qp);
        // Input, previous-picture reference and fixed MV costs remain unchanged,
        // so retries retain vectors/inter prediction. Current-picture neighbors
        // and intra/inter choices are rebuilt at the new quantization.
        auto picture=encoder.encode(intra,header.contexts.data(),qp,false,aqPressure,pass>1,quality%4,poc);
        // Count the actual Annex-B bytes for bitrate feedback. No beta-only
        // access-unit ceiling: Apollo's common packetizer handles this picture.
        size_t packetBytes=intra?headers.parameterSets.size():0;packetBytes+=6;
        unsigned zeros=0;
        for(const auto* part:{&header.bytes,&picture.bytes})for(uint8_t byte:*part) {
            if(zeros==2 && byte<=3){packetBytes++;zeros=0;}
            packetBytes++;zeros=byte?0:zeros+1;
        }
        if(pass>1)rate.learn(intra,earlierQuality,earlierBytes,quality,packetBytes);
        const bool softOverflow=rate.enabled() && double(packetBytes)>budget*1.30;
        if(softOverflow && pass<3) {
            const unsigned raised=rate.raise(quality,double(packetBytes),budget,intra);
            if(raised>quality) {
                earlierQuality=quality;earlierBytes=packetBytes;quality=raised;
                continue;
            }
        }
        encoder.commitPicture();rate.observe(intra,quality,packetBytes,budget);
        return {std::move(header),std::move(picture),qp,pass,aqPressure,budget,quality};
    }
    throw std::runtime_error("Unreachable GPU picture budget state");
}
