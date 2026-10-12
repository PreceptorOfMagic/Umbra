// SPDX-License-Identifier: GPL-3.0-only
#include "frame_encoder.h"
#include "picture_test_io.h"
#include "d3d11_test_backend.h"
#include "d3d11_pattern.h"
int main(int argc,char** argv){try {
    std::cout<<std::unitbuf;
    if(argc!=9 && argc!=10)throw std::runtime_error("Usage: gpu_rate_picture_smoke kernels output-prefix NVIDIA|AMD width height frames mode bitrate-kbps(0=QP-sweep) [flags:4=noise,8=static,16=CU-QP,32=pan,64=deblock,128=source-motion,256=intra-CU,512=holdout]");
    const auto w=uint32_t(std::stoul(argv[4])),h=uint32_t(std::stoul(argv[5])),frames=uint32_t(std::stoul(argv[6])),mode=uint32_t(std::stoul(argv[7])),kbps=uint32_t(std::stoul(argv[8]));
    if(mode>2 || (mode==2 && X265_DEPTH!=10) || frames<16 || frames>300)throw std::runtime_error("Unsupported rate smoke arguments");
    const auto patternFlags=argc==10?uint32_t(std::stoul(argv[9])):4u;
    if(patternFlags&~1020u)throw std::runtime_error("Unsupported pattern flags");
    const std::filesystem::path dir(argv[1]);OpenCL cl;D3D11Sharing sharing(cl,argv[3]);
    Device d(cl,sharing.id,pictureProgram(dir),true,"-cl-std=CL1.2",sharing.properties.data());
    initPictureKernels(d,sharing.id,dir,X265_DEPTH);
    addProgram(d,sharing.id,readFile(dir/"capture.cl"),"-cl-std=CL1.2",{{"capture_yuv","capture_yuv"}});
    aurora_headers::Writer headers(w,h,X265_DEPTH,mode==2,(patternFlags&16)!=0,(patternFlags&64)!=0,(patternFlags&256)!=0);PictureConfig config;config.width=w;config.height=h;config.depth=X265_DEPTH;config.mvCosts=headers.mvCosts;config.adaptiveQp=(patternFlags&16)!=0;config.deblock=(patternFlags&64)!=0;config.isolatedIntra=(patternFlags&256)!=0;config.originalMotion=(patternFlags&128)!=0;
    GpuPictureEncoder encoder(d,config);PictureRateControl rate(kbps,kbps?165:0);ParallelCabac dispatch(d,1,1);Buffer error(d,4);
    SharedD3DImage image(sharing,d,w,h,mode?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM);Pattern pattern(sharing.device.Get());
    const std::string prefix=argv[2];std::ofstream stream(prefix+".hevc",std::ios::binary),recon(prefix+".recon.yuv",std::ios::binary);
    append(stream,headers.parameterSets);uint32_t poc=0;uint64_t bytes=0;unsigned retries=0;
    for(uint32_t frame=0;frame<frames;frame++) {
        const bool intra=frame==0 || frame==9;if(intra)poc=0;
        auto start=std::chrono::steady_clock::now();pattern.draw(image.view.Get(),patternFlags&8?0:frame,mode|(patternFlags&548),w,h);image.begin();dispatch.zero(error,4,nullptr);
        dispatch.call("capture_yuv",size_t(w)*h,256,nullptr,image.image,w,h,cl_uint(X265_DEPTH),mode,
            encoder.planes[0]->input.mem,encoder.planes[1]->input.mem,encoder.planes[2]->input.mem,error.mem);image.end();
        RateControlledPicture encoded;
        if(kbps)encoded=encodePicture(encoder,headers,rate,poc,intra,1.0/165);
        else {static const unsigned qps[]={28,36,51,32,44,30,40,48};encoded.qp=qps[frame%8];encoded.passes=1;
            encoded.header=headers.slice(poc,intra,encoded.qp);encoded.picture=encoder.encode(intra,encoded.header.contexts.data(),encoded.qp);}
        check(cl.clFinish(d.queue),"component completion");
        auto end=std::chrono::steady_clock::now();uint32_t errors;encoder.read(error,4,&errors);if(errors)throw std::runtime_error("Input conversion failed");
        const auto before=stream.tellp();writeNal(stream,encoded.header.nalType,encoded.header.bytes,encoded.picture.bytes);
        const auto size=size_t(stream.tellp()-before);bytes+=size;retries+=encoded.passes-1;
        std::cout<<"RATE_FRAME frame="<<frame<<" intra="<<intra<<" qp="<<encoded.qp<<" target_bytes="<<encoded.targetBytes<<" quality="<<encoded.quality<<" aq_pressure="<<encoded.aqPressure<<" passes="<<encoded.passes<<" bytes="<<size<<" submit_ms="<<std::chrono::duration<double,std::milli>(end-start).count()<<" motion_ms="<<encoded.picture.stages["motion_search"]<<'\n';
        if(!intra && (patternFlags&32))encoder.auditMotion(frame);
        if(!intra && (patternFlags&512) && (frame==1 || frame==31 || frame+1==frames)) {
            std::vector<int32_t> mv(size_t(w/32)*(h/32)*4);encoder.read(encoder.priorVectors,mv.size()*4,mv.data());
            std::map<std::pair<int,int>,unsigned> histogram;
            for(size_t i=0;i<mv.size();i+=4)histogram[{mv[i+2],mv[i+3]}]++;
            std::vector<std::pair<unsigned,std::pair<int,int>>> ranked;
            for(auto& item:histogram)ranked.push_back({item.second,item.first});
            std::sort(ranked.rbegin(),ranked.rend());
            for(size_t i=0;i<std::min(size_t(8),ranked.size());i++)std::cout<<"MOTION_TOP frame="<<frame<<" rank="<<i<<" blocks="<<ranked[i].first<<" x="<<ranked[i].second.first<<" y="<<ranked[i].second.second<<'\n';
        }
        for(unsigned c=0;c<3;c++) {auto pixels=encoder.reconstruction(c);
            // Independent quality observer after the timed encode. These pixels
            // never feed the encoder; compare input against its reconstruction.
            std::vector<uint16_t> source(pixels.size());encoder.read(encoder.planes[c]->input,source.size()*2,source.data());
            uint64_t squared=0;unsigned maximum=0;
            for(size_t i=0;i<pixels.size();i++) {int delta=int(pixels[i])-int(source[i]);squared+=uint64_t(delta*delta);maximum=std::max(maximum,unsigned(std::abs(delta)));}
            double mse=double(squared)/pixels.size(),peak=double((1u<<X265_DEPTH)-1);
            std::cout<<"QUALITY frame="<<frame<<" plane="<<c<<" mse="<<mse<<" psnr="<<(mse?10*std::log10(peak*peak/mse):100)<<" max_error="<<maximum<<'\n';
            if(frame+1==frames){std::ofstream original(prefix+".last-input-"+std::to_string(c)+".u16",std::ios::binary);original.write(reinterpret_cast<const char*>(source.data()),std::streamsize(source.size()*2));}
            if constexpr(X265_DEPTH==8){std::vector<uint8_t> packed(pixels.size());std::transform(pixels.begin(),pixels.end(),packed.begin(),[](uint16_t v){return uint8_t(v);});append(recon,packed);}
            else if(!recon.write(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()*2)))throw std::runtime_error("Recon write failed");}
        poc++;
    }
    std::cout<<"RATE COMPLETE frames="<<frames<<" bitrate_at_165_kbps="<<double(bytes)*8*165/frames/1000<<" retries="<<retries<<" independent_decode_check_required=1\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
