// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "picture_test_backend.h"
struct PictureHeaders:PictureConfig {
    uint32_t frames;
    std::vector<uint8_t> parameterSets;
    std::vector<uint32_t> types;
    std::vector<std::vector<uint8_t>> slices,contexts;
    explicit PictureHeaders(const char* path) {
        auto bytes=readFile(path);size_t at=8;
        if(bytes.size()<32 || memcmp(bytes.data(),"AXHEAD02",8)) throw std::runtime_error("Invalid header package");
        auto word=[&](){if(at+4>bytes.size())throw std::runtime_error("Truncated header");uint32_t v=le32(bytes.data()+at);at+=4;return v;};
        auto take=[&](size_t n){if(n>bytes.size()-at)throw std::runtime_error("Truncated payload");std::vector<uint8_t> v(bytes.begin()+at,bytes.begin()+at+n);at+=n;return v;};
        width=word();height=word();depth=word();qp=word();frames=word();
        if(width<64 || height<64 || width>8192 || height>8192 || width%32 || height%32 ||
            (depth!=8 && depth!=10) || qp!=28 || frames<2 || frames>1024) throw std::runtime_error("Unsupported proof contract");
        parameterSets=take(word());
        auto costs=take(257*2);mvCosts.resize(257);memcpy(mvCosts.data(),costs.data(),costs.size());
        for(uint32_t i=0;i<frames;i++){types.push_back(word());slices.push_back(take(word()));contexts.push_back(take(160));}
        if(at!=bytes.size())throw std::runtime_error("Trailing header data");
    }
};
static void append(std::ofstream& f,const std::vector<uint8_t>& data) {
    if(!f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size())))throw std::runtime_error("Output write failed");
}
static void writeNal(std::ofstream& f,uint32_t type,const std::vector<uint8_t>& header,const std::vector<uint8_t>& data) {
    std::vector<uint8_t> nal={0,0,0,1,uint8_t(type<<1),1}; unsigned zeros=0;
    for(const auto* part:{&header,&data})for(uint8_t v:*part){
        if(zeros==2 && v<=3){nal.push_back(3);zeros=0;}
        nal.push_back(v);zeros=v?0:zeros+1;
    }
    append(f,nal);
}
