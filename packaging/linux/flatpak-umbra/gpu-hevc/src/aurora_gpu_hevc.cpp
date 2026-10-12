// SPDX-License-Identifier: GPL-3.0-only
// Experimental host library: unsplit CU32 with per-picture QP feedback.
// Live integration is smoke-tested; release quality and game-load parity remain unqualified.
#define AURORA_GPU_HEVC_EXPORTS
#include "aurora_gpu_hevc.h"
#include "picture_encoder.h"
#include "d3d11_interop.h"
#include "frame_encoder.h"
#include <cstdio>
static void errorMessage(char* out,size_t capacity,const char* text){if(out && capacity)std::snprintf(out,capacity,"%s",text);}
static agh_config checked(const agh_config* c) {
    if(!c || c->size!=sizeof(agh_config) || c->abi_version!=AGH_ABI_VERSION || !c->d3d11_device || !c->kernel_directory || !*c->kernel_directory ||
       c->width<64 || c->height<64 || c->width>8192 || c->height>8192 || c->width%32 || c->height%32 || c->bit_depth!=X265_DEPTH || c->hdr>1 || (c->hdr && c->bit_depth!=10))
        throw std::runtime_error("Unsupported GPU HEVC configuration or ABI");
    return *c;
}
struct agh_session {
    agh_config config;
    std::filesystem::path kernelDirectory;
    OpenCL cl;
    D3D11Sharing sharing;
    Device device;
    aurora_headers::Writer headers;
    PictureRateControl rate;
    std::chrono::steady_clock::time_point lastSubmission;
    std::unique_ptr<GpuPictureEncoder> encoder;
    ParallelCabac dispatch;
    Buffer conversionError;
    std::unique_ptr<SharedD3DImage> input;
    DXGI_FORMAT inputFormat=DXGI_FORMAT_UNKNOWN;
    uint32_t poc=0;
    bool haveFrame=false,failed=false;
    uint64_t lastFrame=0;
    std::vector<uint8_t> packet;
    explicit agh_session(const agh_config* requested):config(checked(requested)),kernelDirectory(config.kernel_directory),
        sharing(cl,static_cast<ID3D11Device*>(config.d3d11_device)),
        device(cl,sharing.id,pictureProgram(kernelDirectory),true,"-cl-std=CL1.2",sharing.properties.data()),
        headers(config.width,config.height,config.bit_depth,config.hdr!=0,config.bitrate_kbps!=0,config.bitrate_kbps!=0,config.bitrate_kbps!=0),
        rate(config.bitrate_kbps,config.framerate),dispatch(device,1,1),conversionError(device,4) {
        initPictureKernels(device,sharing.id,kernelDirectory,config.bit_depth);
        addProgram(device,sharing.id,readFile(kernelDirectory/"capture.cl"),"-cl-std=CL1.2",{{"capture_yuv","capture_yuv"}});
        PictureConfig p;p.width=config.width;p.height=config.height;p.depth=config.bit_depth;p.mvCosts=headers.mvCosts;p.adaptiveQp=config.bitrate_kbps!=0;p.deblock=p.isolatedIntra=p.originalMotion=config.bitrate_kbps!=0;
        encoder=std::make_unique<GpuPictureEncoder>(device,p);
    }
    void encode(ID3D11Texture2D* texture,uint64_t frame,bool requestedIdr,agh_packet& out) {
        if(failed)throw std::runtime_error("GPU encoder failed earlier; close and recreate the session");
        if(!texture || (haveFrame && frame<=lastFrame))throw std::runtime_error("Missing GPU capture or non-increasing frame identity");
        D3D11_TEXTURE2D_DESC desc;texture->GetDesc(&desc);ComPtr<ID3D11Device> owner;texture->GetDevice(&owner);
        if(owner.Get()!=sharing.device.Get() || desc.Width!=config.width || desc.Height!=config.height || desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
           (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT) ||
           (config.hdr && desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))throw std::runtime_error("Capture surface does not match GPU HEVC contract");
        auto start=std::chrono::steady_clock::now();
        if(!input || desc.Format!=inputFormat){input=std::make_unique<SharedD3DImage>(sharing,device,config.width,config.height,desc.Format);inputFormat=desc.Format;}
        bool intra=!haveFrame || requestedIdr;if(intra)poc=0;
        sharing.context->CopyResource(input->texture.Get(),texture);sharing.context->Flush();
        input->begin();dispatch.zero(conversionError,4,nullptr);
        cl_uint mode=config.hdr?2:desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?1:0;
        dispatch.call("capture_yuv",size_t(config.width)*config.height,256,nullptr,input->image,config.width,config.height,config.bit_depth,mode,
            encoder->planes[0]->input.mem,encoder->planes[1]->input.mem,encoder->planes[2]->input.mem,conversionError.mem);
        input->end();
        double elapsed=haveFrame?std::chrono::duration<double>(start-lastSubmission).count():1.0/(config.framerate?config.framerate:60);
        auto result=encodePicture(*encoder,headers,rate,poc,intra,elapsed);
        auto& coded=result.picture;auto& syntax=result.header;
        uint32_t error;encoder->read(conversionError,4,&error);if(error)throw std::runtime_error("GPU input conversion failed");
        packet.clear();if(intra)packet.insert(packet.end(),headers.parameterSets.begin(),headers.parameterSets.end());
        const uint8_t prefix[]={0,0,0,1,uint8_t(syntax.nalType<<1),1};packet.insert(packet.end(),std::begin(prefix),std::end(prefix));
        unsigned zeros=0;for(const auto* part:{&syntax.bytes,&coded.bytes})for(uint8_t v:*part){
            if(zeros==2 && v<=3){packet.push_back(3);zeros=0;}packet.push_back(v);zeros=v?0:zeros+1;
        }
        auto end=std::chrono::steady_clock::now();
        out.is_idr=intra?1:0;out.data=packet.data();out.bytes=packet.size();out.frame_id=frame;
        out.submit_to_packet_ms=std::chrono::duration<double,std::milli>(end-start).count();
        out.qp=result.qp;out.encode_passes=result.passes;
        poc++;haveFrame=true;lastFrame=frame;lastSubmission=start;
    }
};
extern "C" AGH_API agh_session* agh_open(const agh_config* config,char* error,size_t capacity) {
    errorMessage(error,capacity,"");try{return new agh_session(config);}catch(const std::exception& e){errorMessage(error,capacity,e.what());}catch(...){errorMessage(error,capacity,"Unknown GPU encoder initialization failure");}return nullptr;
}
extern "C" AGH_API int agh_encode(agh_session* session,void* texture,uint64_t frame,uint32_t forceIdr,agh_packet* out,char* error,size_t capacity) {
    errorMessage(error,capacity,"");
    if(!session || !out || out->size!=sizeof(agh_packet) || forceIdr>1){errorMessage(error,capacity,"Invalid GPU encoder call");return -1;}
    out->data=nullptr;out->bytes=0;
    try{session->encode(static_cast<ID3D11Texture2D*>(texture),frame,forceIdr!=0,*out);return 0;}
    catch(const std::exception& e){errorMessage(error,capacity,e.what());}catch(...){errorMessage(error,capacity,"Unknown GPU encode failure");}
    session->failed=true;return -1;
}
extern "C" AGH_API void agh_close(agh_session* session){delete session;}
