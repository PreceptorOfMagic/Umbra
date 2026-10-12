// SPDX-License-Identifier: GPL-2.0-or-later
// Complete-picture development proof. GPU pixel coding + syntax + CABAC.
// Input/output files are test artifacts, not a live Aurora/Apollo integration.
#include "picture_test_io.h"
static void run(Device& d,const PictureHeaders& h,const char* inputPath,const std::string& outputPrefix) {
    GpuPictureEncoder encoder(d,h);
    std::ifstream input(inputPath,std::ios::binary);if(!input)throw std::runtime_error("Input unavailable");
    std::ofstream bitstream(outputPrefix+".hevc",std::ios::binary),reconstruction(outputPrefix+".recon.yuv",std::ios::binary);
    append(bitstream,h.parameterSets);
    for(uint32_t frame=0;frame<h.frames;frame++) {
        bool intra=h.types[frame]==20;
        for(unsigned c=0;c<3;c++) {
            size_t samples=size_t(h.width>>(c?1:0))*(h.height>>(c?1:0));
            std::vector<uint16_t> pixels(samples);
            if(h.depth==8) {
                std::vector<uint8_t> packed(samples);
                if(!input.read(reinterpret_cast<char*>(packed.data()),std::streamsize(packed.size())))throw std::runtime_error("Short raw frame");
                std::copy(packed.begin(),packed.end(),pixels.begin());
            }else if(!input.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(samples*2)))throw std::runtime_error("Short raw frame");
            encoder.upload(c,pixels);
        }
        auto coded=encoder.encode(intra,h.contexts[frame].data());
        writeNal(bitstream,h.types[frame],h.slices[frame],coded.bytes);
        if(!intra)encoder.auditMotion(frame);
        for(unsigned c=0;c<3;c++) {
            auto pixels=encoder.reconstruction(c);
            if(h.depth==8){std::vector<uint8_t> packed(pixels.size());std::transform(pixels.begin(),pixels.end(),packed.begin(),[](uint16_t v){return uint8_t(v);});append(reconstruction,packed);}
            else if(!reconstruction.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()*2)))throw std::runtime_error("Recon write failed");
        }
        std::cout<<"FRAME "<<frame<<" type="<<(intra?'I':'P')<<" ops="<<coded.operations<<" bytes="<<coded.bytes.size()<<" device_span_ms="<<coded.deviceSpanMs
            <<" submit_read_ms="<<coded.submitReadMs<<'\n';
        if(frame<2)for(const auto& item:coded.stages)std::cout<<"STAGE frame="<<frame<<' '<<item.first<<" ms="<<item.second<<'\n';
    }
    if(input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Input contains extra frames");
    std::cout<<"ENCODE COMPLETE frames="<<h.frames<<" output="<<outputPrefix<<" independent_decode_check_required=1\n";
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        if(argc!=6)throw std::runtime_error("Usage: gpu_picture_smoke kernel-directory headers raw-yuv output-prefix vendor");
        PictureHeaders headers(argv[2]);std::filesystem::path dir(argv[1]);auto source=pictureProgram(dir);
        OpenCL cl;cl_uint n=0;check(cl.clGetPlatformIDs(0,nullptr,&n),"platform count");
        std::vector<cl_platform_id> platforms(n);check(cl.clGetPlatformIDs(n,platforms.data(),nullptr),"platforms");unsigned tested=0;
        for(auto platform:platforms){
            cl_int e=cl.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,0,nullptr,&n);if(e==CL_DEVICE_NOT_FOUND)continue;
            check(e,"device count");std::vector<cl_device_id> devices(n);check(cl.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,n,devices.data(),nullptr),"devices");
            for(auto id:devices){
                if(cl.info(id,CL_DEVICE_VENDOR).find(argv[5])==std::string::npos)continue;
                std::cout<<"GPU name="<<cl.info(id,CL_DEVICE_NAME)<<" driver="<<cl.info(id,CL_DRIVER_VERSION)<<" index="<<tested<<'\n';
                Device d(cl,id,source,true);
                initPictureKernels(d,id,dir,headers.depth);
                run(d,headers,argv[3],std::string(argv[4])+"-gpu"+std::to_string(tested++));
            }
        }
        if(!tested)throw std::runtime_error("No matching GPU; no CPU fallback");
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
