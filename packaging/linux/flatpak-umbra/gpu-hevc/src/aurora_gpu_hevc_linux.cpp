// SPDX-License-Identifier: GPL-3.0-only
#include "aurora_gpu_hevc_linux.h"
#include "frame_encoder.h"
#include <cstdio>
#include <limits>

namespace {
void message(char* out,size_t capacity,const char* text) {
    if(out && capacity)std::snprintf(out,capacity,"%s",text);
}
struct LinuxCL:OpenCL {
    decltype(&::clCreateImage) createImage=nullptr;
    decltype(&::clEnqueueWriteImage) writeImage=nullptr;
    cl_device_id id=nullptr;
    std::string name;
    LinuxCL() {
        createImage=reinterpret_cast<decltype(createImage)>(dlsym(module,"clCreateImage"));
        writeImage=reinterpret_cast<decltype(writeImage)>(dlsym(module,"clEnqueueWriteImage"));
        if(!createImage || !writeImage)throw std::runtime_error("OpenCL 1.2 image API unavailable");
        cl_uint count=0;check(clGetPlatformIDs(0,nullptr,&count),"platform count");
        std::vector<cl_platform_id> platforms(count);
        if(count)check(clGetPlatformIDs(count,platforms.data(),nullptr),"platforms");
        unsigned found=0;
        for(auto p:platforms) {
            cl_uint n=0;auto e=clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,0,nullptr,&n);
            if(e==CL_DEVICE_NOT_FOUND)continue;
            check(e,"GPU count");std::vector<cl_device_id> ids(n);
            if(n)check(clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,n,ids.data(),nullptr),"GPUs");
            for(auto candidate:ids) {
                cl_bool available=CL_FALSE,images=CL_FALSE,compiler=CL_FALSE;
                check(clGetDeviceInfo(candidate,CL_DEVICE_AVAILABLE,sizeof available,&available,nullptr),"GPU availability");
                check(clGetDeviceInfo(candidate,CL_DEVICE_IMAGE_SUPPORT,sizeof images,&images,nullptr),"GPU images");
                check(clGetDeviceInfo(candidate,CL_DEVICE_COMPILER_AVAILABLE,sizeof compiler,&compiler,nullptr),"GPU compiler");
                if(available && images && compiler){id=candidate;found++;}
            }
        }
        if(found!=1)throw std::runtime_error(found?"Multiple OpenCL GPUs; explicit device selection required":"No usable OpenCL GPU; no CPU fallback");
        name=info(id,CL_DEVICE_NAME);
    }
};
aghl_config checked(const aghl_config* c) {
    if(!c || c->size!=sizeof *c || c->abi_version!=AGHL_ABI_VERSION || !c->kernel_directory || !*c->kernel_directory ||
       c->width<64 || c->height<64 || c->width>8192 || c->height>8192 || c->width%32 || c->height%32 ||
       c->bit_depth!=X265_DEPTH || c->hdr>1 || (c->hdr && c->bit_depth!=10) || !c->bitrate_kbps || !c->framerate)
        throw std::runtime_error("Unsupported Linux GPU HEVC configuration or ABI");
    return *c;
}
struct Image {
    LinuxCL& cl;cl_mem mem=nullptr;
    Image(LinuxCL& api,Device& d,uint32_t w,uint32_t h,cl_channel_order order,cl_channel_type type):cl(api) {
        cl_image_format format{order,type};cl_image_desc desc{};
        desc.image_type=CL_MEM_OBJECT_IMAGE2D;desc.image_width=w;desc.image_height=h;
        cl_int e;mem=cl.createImage(d.context,CL_MEM_READ_WRITE,&format,&desc,nullptr,&e);check(e,"capture image");
    }
    ~Image(){if(mem)cl.clReleaseMemObject(mem);}
    Image(const Image&)=delete;
};
}
struct aghl_session {
    aghl_config config;
    std::filesystem::path directory;
    LinuxCL cl;
    Device device;
    aurora_headers::Writer headers;
    PictureRateControl rate;
    std::unique_ptr<GpuPictureEncoder> encoder;
    ParallelCabac dispatch;
    Buffer conversionError;
    std::unique_ptr<Image> input,fitted,packed;
    uint32_t inputWidth=0,inputHeight=0,inputFormat=0,poc=0;
    bool haveFrame=false,failed=false;
    uint64_t lastFrame=0;
    std::chrono::steady_clock::time_point lastSubmission;
    std::vector<uint8_t> packet;
    explicit aghl_session(const aghl_config* c):config(checked(c)),directory(config.kernel_directory),
        device(cl,cl.id,pictureProgram(directory),true),
        headers(config.width,config.height,config.bit_depth,config.hdr!=0,true,true,true),
        rate(config.bitrate_kbps,config.framerate),dispatch(device,1,1),conversionError(device,4) {
        initPictureKernels(device,cl.id,directory,config.bit_depth);
        addProgram(device,cl.id,readFile(directory/"capture.cl"),"-cl-std=CL1.2",{{"capture_yuv","capture_yuv"}});
        addProgram(device,cl.id,readFile(directory/"linux_capture.cl"),"-cl-std=CL1.2",{{"capture_fit","capture_fit"},{"capture_unpack_pq","capture_unpack_pq"}});
        PictureConfig p;p.width=config.width;p.height=config.height;p.depth=config.bit_depth;p.mvCosts=headers.mvCosts;
        p.adaptiveQp=p.deblock=p.isolatedIntra=p.originalMotion=true;
        encoder=std::make_unique<GpuPictureEncoder>(device,p);
    }
    void encode(const aghl_frame* f,uint64_t frame,bool requestedIdr,agh_packet& out) {
        if(failed)throw std::runtime_error("GPU encoder failed earlier; close and recreate the session");
        if(!f || f->size!=sizeof *f || !f->data || !f->width || !f->height || f->width>8192 || f->height>8192 ||
           f->format<AGHL_BGRA8 || f->format>AGHL_XBGR2101010_PQ ||
           (config.hdr && f->format!=AGHL_SCRGB16F && f->format!=AGHL_XBGR2101010_PQ) ||
           (!config.hdr && f->format==AGHL_XBGR2101010_PQ) ||
           (haveFrame && frame<=lastFrame))throw std::runtime_error("Invalid Linux capture or frame identity");
        const size_t pixel=f->format==AGHL_SCRGB16F?8:4,row=size_t(f->width)*pixel;
        if(f->row_pitch<row || f->row_pitch>std::numeric_limits<size_t>::max()/f->height ||
           f->bytes<f->row_pitch*(f->height-1)+row)throw std::runtime_error("Capture buffer pitch/size mismatch");
        const bool pq=f->format==AGHL_XBGR2101010_PQ;
        const bool linear=f->format==AGHL_SCRGB16F || pq;
        auto start=std::chrono::steady_clock::now();
        if(!input || inputWidth!=f->width || inputHeight!=f->height || inputFormat!=f->format) {
            input.reset();fitted.reset();packed.reset();
            input=std::make_unique<Image>(cl,device,f->width,f->height,f->format==AGHL_BGRA8?CL_BGRA:CL_RGBA,
                linear?CL_HALF_FLOAT:CL_UNORM_INT8);
            if(pq)packed=std::make_unique<Image>(cl,device,f->width,f->height,CL_RGBA,CL_UNSIGNED_INT8);
            inputWidth=f->width;inputHeight=f->height;inputFormat=f->format;
        }
        const size_t origin[3]={0,0,0},region[3]={f->width,f->height,1};
        check(cl.writeImage(device.queue,pq?packed->mem:input->mem,CL_TRUE,origin,region,f->row_pitch,0,f->data,0,nullptr,nullptr),"capture upload");
        if(pq)dispatch.call("capture_unpack_pq",size_t(f->width)*f->height,256,nullptr,packed->mem,input->mem,f->width,f->height);
        cl_mem source=input->mem;
        if(f->width!=config.width || f->height!=config.height) {
            if(!fitted)fitted=std::make_unique<Image>(cl,device,config.width,config.height,CL_RGBA,
                linear?CL_HALF_FLOAT:CL_UNORM_INT8);
            dispatch.call("capture_fit",size_t(config.width)*config.height,256,nullptr,input->mem,fitted->mem,config.width,config.height);
            source=fitted->mem;
        }
        bool intra=!haveFrame || requestedIdr;if(intra)poc=0;
        dispatch.zero(conversionError,4,nullptr);
        cl_uint mode=config.hdr?2:f->format==AGHL_SCRGB16F?1:0;
        dispatch.call("capture_yuv",size_t(config.width)*config.height,256,nullptr,source,config.width,config.height,config.bit_depth,mode,
            encoder->planes[0]->input.mem,encoder->planes[1]->input.mem,encoder->planes[2]->input.mem,conversionError.mem);
        double elapsed=haveFrame?std::chrono::duration<double>(start-lastSubmission).count():1.0/config.framerate;
        auto result=encodePicture(*encoder,headers,rate,poc,intra,elapsed);
        uint32_t error;encoder->read(conversionError,4,&error);if(error)throw std::runtime_error("GPU input conversion failed");
        packet.clear();if(intra)packet.insert(packet.end(),headers.parameterSets.begin(),headers.parameterSets.end());
        const uint8_t prefix[]={0,0,0,1,uint8_t(result.header.nalType<<1),1};packet.insert(packet.end(),std::begin(prefix),std::end(prefix));
        unsigned zeros=0;for(const auto* part:{&result.header.bytes,&result.picture.bytes})for(uint8_t v:*part){
            if(zeros==2 && v<=3){packet.push_back(3);zeros=0;}packet.push_back(v);zeros=v?0:zeros+1;
        }
        out.is_idr=intra?1:0;out.data=packet.data();out.bytes=packet.size();out.frame_id=frame;
        out.submit_to_packet_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        out.qp=result.qp;out.encode_passes=result.passes;
        poc++;haveFrame=true;lastFrame=frame;lastSubmission=start;
    }
};
extern "C" AGHL_API int aghl_probe(char* name,size_t n,char* error,size_t capacity) {
    message(error,capacity,"");message(name,n,"");
    try{LinuxCL cl;message(name,n,cl.name.c_str());return 0;}
    catch(const std::exception& e){message(error,capacity,e.what());}catch(...){message(error,capacity,"Unknown OpenCL probe failure");}return -1;
}
extern "C" AGHL_API aghl_session* aghl_open(const aghl_config* config,char* error,size_t capacity) {
    message(error,capacity,"");try{return new aghl_session(config);}catch(const std::exception& e){message(error,capacity,e.what());}
    catch(...){message(error,capacity,"Unknown Linux GPU initialization failure");}return nullptr;
}
extern "C" AGHL_API int aghl_encode(aghl_session* session,const aghl_frame* frame,uint64_t id,uint32_t force,
                                    agh_packet* out,char* error,size_t capacity) {
    message(error,capacity,"");
    if(!session || !out || out->size!=sizeof *out || force>1){message(error,capacity,"Invalid Linux GPU call");return -1;}
    out->data=nullptr;out->bytes=0;
    try{session->encode(frame,id,force!=0,*out);return 0;}catch(const std::exception& e){message(error,capacity,e.what());}
    catch(...){message(error,capacity,"Unknown Linux GPU encode failure");}session->failed=true;return -1;
}
extern "C" AGHL_API void aghl_close(aghl_session* session){delete session;}
