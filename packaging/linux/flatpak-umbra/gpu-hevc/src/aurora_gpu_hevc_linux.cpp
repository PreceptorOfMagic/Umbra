// SPDX-License-Identifier: GPL-3.0-only
#include "aurora_gpu_hevc_linux.h"
#include "frame_encoder.h"
#include "linux_device_selection.h"
#include <CL/cl_ext.h>
#include <CL/cl_gl.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace {
void message(char* out,size_t capacity,const char* text) {
    if(out && capacity)std::snprintf(out,capacity,"%s",text);
}
// An external frame this runtime cannot import. The session stays usable: aghl_encode returns
// AGHL_EXTERNAL_UNAVAILABLE and the caller resubmits the same frame with host pixels.
struct ExternalUnavailable:std::runtime_error{using std::runtime_error::runtime_error;};
struct LinuxCL:OpenCL {
    decltype(&::clCreateImage) createImage=nullptr;
    decltype(&::clEnqueueWriteImage) writeImage=nullptr;
    decltype(&::clEnqueueCopyBufferToImage) copyBufferToImage=nullptr;
    // OpenCL 3.0 external memory; absent on older runtimes (external frames then fail clearly).
    // cl_mem_properties is cl_ulong (OpenCL 3.0 typedef; this library targets an earlier version).
    using create_with_properties_t=cl_mem(CL_API_CALL*)(cl_context,const cl_ulong*,cl_mem_flags,size_t,void*,cl_int*);
    create_with_properties_t createBufferWithProperties=nullptr;
    // cl_khr_external_memory acquire/release; required around every use of an imported object.
    using external_objects_t=cl_int(CL_API_CALL*)(cl_command_queue,cl_uint,const cl_mem*,cl_uint,const cl_event*,cl_event*);
    external_objects_t acquireExternal=nullptr,releaseExternal=nullptr;
    // cl_khr_gl_sharing (OpenCL 1.2 core entry points in the ICD loader).
    decltype(&::clCreateFromGLTexture) createFromGLTexture=nullptr;
    decltype(&::clEnqueueAcquireGLObjects) acquireGL=nullptr;
    decltype(&::clEnqueueReleaseGLObjects) releaseGL=nullptr;
    decltype(&::clEnqueueCopyImageToBuffer) copyImageToBuffer=nullptr;
    decltype(&::clGetImageInfo) getImageInfo=nullptr;
    cl_device_id id=nullptr;
    std::string name;
    explicit LinuxCL(const char* selector=nullptr) {
        createImage=reinterpret_cast<decltype(createImage)>(dlsym(module,"clCreateImage"));
        writeImage=reinterpret_cast<decltype(writeImage)>(dlsym(module,"clEnqueueWriteImage"));
        if(!createImage || !writeImage)throw std::runtime_error("OpenCL 1.2 image API unavailable");
        copyBufferToImage=reinterpret_cast<decltype(copyBufferToImage)>(dlsym(module,"clEnqueueCopyBufferToImage"));
        createBufferWithProperties=reinterpret_cast<create_with_properties_t>(dlsym(module,"clCreateBufferWithProperties"));
        createFromGLTexture=reinterpret_cast<decltype(createFromGLTexture)>(dlsym(module,"clCreateFromGLTexture"));
        acquireGL=reinterpret_cast<decltype(acquireGL)>(dlsym(module,"clEnqueueAcquireGLObjects"));
        releaseGL=reinterpret_cast<decltype(releaseGL)>(dlsym(module,"clEnqueueReleaseGLObjects"));
        copyImageToBuffer=reinterpret_cast<decltype(copyImageToBuffer)>(dlsym(module,"clEnqueueCopyImageToBuffer"));
        getImageInfo=reinterpret_cast<decltype(getImageInfo)>(dlsym(module,"clGetImageInfo"));
        cl_uint count=0;check(clGetPlatformIDs(0,nullptr,&count),"platform count");
        std::vector<cl_platform_id> platforms(count);
        if(count)check(clGetPlatformIDs(count,platforms.data(),nullptr),"platforms");
        std::vector<cl_device_id> eligible;
        std::vector<aghl::DeviceChoice> choices;
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
                if(!available || !images || !compiler)continue;
                cl_bool unified=CL_FALSE;
                check(clGetDeviceInfo(candidate,CL_DEVICE_HOST_UNIFIED_MEMORY,sizeof unified,&unified,nullptr),"GPU memory topology");
                aghl::DeviceChoice choice{info(candidate,CL_DEVICE_NAME),{},unified!=CL_FALSE};
                const auto extensions=" "+info(candidate,CL_DEVICE_EXTENSIONS)+" ";
                if(extensions.find(" cl_khr_pci_bus_info ")!=std::string::npos) {
                    cl_device_pci_bus_info_khr pci{};
                    if(clGetDeviceInfo(candidate,CL_DEVICE_PCI_BUS_INFO_KHR,sizeof pci,&pci,nullptr)==CL_SUCCESS &&
                       pci.pci_domain<=0xffff && pci.pci_bus<=0xff && pci.pci_device<=0x1f && pci.pci_function<=7) {
                        char address[32];
                        std::snprintf(address,sizeof address,"%04x:%02x:%02x.%x",pci.pci_domain,pci.pci_bus,pci.pci_device,pci.pci_function);
                        choice.pci=address;
                    }
                }
                eligible.push_back(candidate);choices.push_back(std::move(choice));
            }
        }
        auto selected=aghl::selectDevice(choices,selector?selector:"");
        id=eligible[selected];name=choices[selected].name;
        if(!choices[selected].pci.empty())name+=" pci:"+choices[selected].pci;
    }
};
aghl_config checked(const aghl_config* in) {
    if(!in || (in->size!=sizeof *in && in->size!=AGHL_CONFIG_BASE_SIZE))
        throw std::runtime_error("Unsupported Linux GPU HEVC configuration or ABI");
    aghl_config copy{};std::memcpy(&copy,in,in->size);copy.size=sizeof copy;  // fields a base-size caller omits stay null
    const aghl_config* c=&copy;
    if(c->abi_version!=AGHL_ABI_VERSION || !c->kernel_directory || !*c->kernel_directory ||
       c->width<64 || c->height<64 || c->width>8192 || c->height>8192 || c->width%2 || c->height%2 ||
       c->bit_depth!=X265_DEPTH || c->hdr>1 || (c->hdr && c->bit_depth!=10) || !c->bitrate_kbps || !c->framerate)
        throw std::runtime_error("Unsupported Linux GPU HEVC configuration or ABI");
    return copy;
}
// The OpenCL context shares the caller's EGL context where the GPU's OpenCL offers that
// (cl_khr_gl_sharing for this display and context); otherwise a plain context, as before.
struct GlSharing {
    cl_context_properties props[7]{};
    bool active=false;
    GlSharing(LinuxCL& cl,const aghl_config& c) {
        if(!c.egl_display || !c.egl_context || !cl.createFromGLTexture || !cl.acquireGL || !cl.releaseGL || !cl.copyImageToBuffer || !cl.getImageInfo)return;
        cl_platform_id platform=nullptr;
        if(cl.clGetDeviceInfo(cl.id,CL_DEVICE_PLATFORM,sizeof platform,&platform,nullptr)!=CL_SUCCESS || !platform)return;
        try{if((" "+cl.info(cl.id,CL_DEVICE_EXTENSIONS)+" ").find(" cl_khr_gl_sharing ")==std::string::npos)return;}catch(...){return;}
        const cl_context_properties wanted[7]={CL_CONTEXT_PLATFORM,cl_context_properties(platform),
            CL_GL_CONTEXT_KHR,cl_context_properties(c.egl_context),CL_EGL_DISPLAY_KHR,cl_context_properties(c.egl_display),0};
        cl_int e=CL_SUCCESS;
        cl_context probe=cl.clCreateContext(wanted,1,&cl.id,nullptr,nullptr,&e);
        if(e!=CL_SUCCESS || !probe)return;  // e.g. NVIDIA: EGL sharing refused (-59)
        cl.clReleaseContext(probe);
        std::memcpy(props,wanted,sizeof props);active=true;
    }
    const cl_context_properties* properties() const {return active?props:nullptr;}
};
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
    const uint32_t codedWidth,codedHeight;
    std::filesystem::path directory;
    LinuxCL cl;
    GlSharing glShare;
    Device device;
    aurora_headers::Writer headers;
    PictureRateControl rate;
    std::unique_ptr<GpuPictureEncoder> encoder;
    ParallelCabac dispatch;
    Buffer conversionError;
    std::unique_ptr<Image> input,fitted,packed;
    uint32_t inputWidth=0,inputHeight=0,inputFormat=0,poc=0;
    // Imported external allocation, cached by the caller's (fd, bytes); the caller keeps its fd open
    // and the allocation stable for the session. A successful import transfers fd ownership to
    // OpenCL (cl_khr_external_memory), so a duplicate is imported and the caller's fd stays its own.
    cl_mem external=nullptr;
    int32_t externalFd=-1;
    size_t externalBytes=0;
    uint64_t externalSerial=0;
    cl_mem importExternal(int32_t fd,size_t bytes,uint64_t serial) {
        if(external && fd==externalFd && bytes==externalBytes && serial==externalSerial)return external;
        if(external){cl.clReleaseMemObject(external);external=nullptr;externalFd=-1;externalBytes=0;}
        if(!cl.createBufferWithProperties || !cl.copyBufferToImage)
            throw ExternalUnavailable("OpenCL external memory unavailable (needs OpenCL 3.0 + cl_khr_external_memory_opaque_fd)");
        if(!cl.acquireExternal) {
            cl_platform_id platform=nullptr;
            using address_t=void*(CL_API_CALL*)(cl_platform_id,const char*);
            auto address=reinterpret_cast<address_t>(dlsym(cl.module,"clGetExtensionFunctionAddressForPlatform"));
            if(address && cl.clGetDeviceInfo(cl.id,CL_DEVICE_PLATFORM,sizeof platform,&platform,nullptr)==CL_SUCCESS) {
                cl.acquireExternal=reinterpret_cast<LinuxCL::external_objects_t>(address(platform,"clEnqueueAcquireExternalMemObjectsKHR"));
                cl.releaseExternal=reinterpret_cast<LinuxCL::external_objects_t>(address(platform,"clEnqueueReleaseExternalMemObjectsKHR"));
            }
            if(!cl.acquireExternal || !cl.releaseExternal)
                throw ExternalUnavailable("OpenCL external memory acquire/release unavailable");
        }
        const int copy=fcntl(fd,F_DUPFD_CLOEXEC,0);
        if(copy<0)throw ExternalUnavailable("external frame fd cannot be duplicated");
        const cl_ulong props[]={CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_FD_KHR,cl_ulong(copy),0};
        cl_int e=CL_SUCCESS;
        external=cl.createBufferWithProperties(device.context,props,CL_MEM_READ_ONLY,bytes,nullptr,&e);
        if(e!=CL_SUCCESS || !external) {
            external=nullptr;close(copy);  // ownership transfers only on success
            throw ExternalUnavailable("external frame import failed (OpenCL error "+std::to_string(e)+")");
        }
        externalFd=fd;externalBytes=bytes;externalSerial=serial;
        return external;
    }
    // GL texture frames: the imported image (cached by serial) and a staging buffer, because a
    // GL_R32UI image cannot be copied straight into the RGBA input image (formats must match).
    cl_mem glImage=nullptr,staging=nullptr;
    uint64_t glSerial=0;
    size_t stagingBytes=0;
    cl_mem importGl(uint32_t texture,uint64_t serial,size_t bytes) {
        if(!glShare.active)throw ExternalUnavailable("GL texture frame without a GL-shared OpenCL context");
        if(!glImage || serial!=glSerial) {
            if(glImage){cl.clReleaseMemObject(glImage);glImage=nullptr;}
            cl_int e=CL_SUCCESS;
            cl_mem image=cl.createFromGLTexture(device.context,CL_MEM_READ_ONLY,0x0DE1 /* GL_TEXTURE_2D */,0,texture,&e);
            if(e!=CL_SUCCESS || !image)throw ExternalUnavailable("GL texture import failed (OpenCL error "+std::to_string(e)+")");
            size_t element=0;
            if(cl.getImageInfo(image,CL_IMAGE_ELEMENT_SIZE,sizeof element,&element,nullptr)!=CL_SUCCESS || element!=4) {
                cl.clReleaseMemObject(image);
                throw ExternalUnavailable("GL texture is not 4 bytes per pixel");
            }
            glImage=image;glSerial=serial;
        }
        if(!staging || stagingBytes<bytes) {
            if(staging){cl.clReleaseMemObject(staging);staging=nullptr;}
            cl_int e=CL_SUCCESS;
            staging=cl.clCreateBuffer(device.context,CL_MEM_READ_WRITE,bytes,nullptr,&e);check(e,"GL frame staging");
            stagingBytes=bytes;
        }
        return glImage;
    }
    ~aghl_session(){
        if(external)cl.clReleaseMemObject(external);
        if(glImage)cl.clReleaseMemObject(glImage);
        if(staging)cl.clReleaseMemObject(staging);
    }
    bool haveFrame=false,failed=false;
    uint64_t lastFrame=0;
    std::chrono::steady_clock::time_point lastSubmission;
    std::vector<uint8_t> packet;
    explicit aghl_session(const aghl_config* c):config(checked(c)),codedWidth((config.width+31)&~31u),codedHeight((config.height+31)&~31u),directory(config.kernel_directory),cl(config.device_selector),
        glShare(cl,config),device(cl,cl.id,pictureProgram(directory),true,"-cl-std=CL1.2",glShare.properties()),
        headers(codedWidth,codedHeight,config.bit_depth,config.hdr!=0,true,true,true,config.width,config.height),
        rate(config.bitrate_kbps,config.framerate),dispatch(device,1,1),conversionError(device,4) {
        initPictureKernels(device,cl.id,directory,config.bit_depth);
        addProgram(device,cl.id,readFile(directory/"capture.cl"),"-cl-std=CL1.2",{{"capture_yuv","capture_yuv"}});
        addProgram(device,cl.id,readFile(directory/"linux_capture.cl"),"-cl-std=CL1.2",{{"capture_fit","capture_fit"},{"capture_unpack_pq","capture_unpack_pq"}});
        PictureConfig p;p.width=codedWidth;p.height=codedHeight;p.depth=config.bit_depth;p.mvCosts=headers.mvCosts;
        p.adaptiveQp=p.deblock=p.isolatedIntra=p.originalMotion=true;
        encoder=std::make_unique<GpuPictureEncoder>(device,p);
    }
    void encode(const aghl_frame* f,uint64_t frame,bool requestedIdr,agh_packet& out) {
        if(failed)throw std::runtime_error("GPU encoder failed earlier; close and recreate the session");
        const bool externalFrame=f && f->size>=sizeof *f && (f->flags&AGHL_FRAME_EXTERNAL);
        const bool glFrame=f && f->size>=sizeof *f && (f->flags&AGHL_FRAME_GL_TEXTURE);
        if(!f || (f->size!=sizeof *f && f->size!=AGHL_FRAME_BASE_SIZE) || (!externalFrame && !glFrame && !f->data) ||
           (externalFrame && glFrame) || (externalFrame && f->external_fd<0) || (glFrame && !f->gl_texture) || !f->width || !f->height || f->width>8192 || f->height>8192 ||
           f->format<AGHL_BGRA8 || f->format>AGHL_XBGR2101010_PQ ||
           (config.hdr && f->format!=AGHL_SCRGB16F && f->format!=AGHL_XBGR2101010_PQ) ||
           (!config.hdr && f->format==AGHL_XBGR2101010_PQ) ||
           (haveFrame && frame<=lastFrame))throw std::runtime_error("Invalid Linux capture or frame identity");
        const size_t pixel=f->format==AGHL_SCRGB16F?8:4,row=size_t(f->width)*pixel;
        if(f->row_pitch<row || f->row_pitch>std::numeric_limits<size_t>::max()/f->height ||
           f->bytes<f->row_pitch*(f->height-1)+row || ((externalFrame || glFrame) && f->row_pitch!=row))
            throw std::runtime_error("Capture buffer pitch/size mismatch");
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
        if(externalFrame) {
            // GPU to GPU: the frame never visits host memory. The producer finished writing before
            // this call (its own synchronisation); acquire/release order OpenCL's use of it.
            cl_mem source=importExternal(f->external_fd,f->bytes,f->external_serial);
            check(cl.acquireExternal(device.queue,1,&source,0,nullptr,nullptr),"external frame acquire");
            check(cl.copyBufferToImage(device.queue,source,pq?packed->mem:input->mem,0,origin,region,0,nullptr,nullptr),"external frame copy");
            check(cl.releaseExternal(device.queue,1,&source,0,nullptr,nullptr),"external frame release");
        } else if(glFrame) {
            // GPU to GPU through the shared GL context (Mesa rusticl and other cl_khr_gl_sharing runtimes).
            cl_mem source=importGl(f->gl_texture,f->external_serial,f->bytes);
            check(cl.acquireGL(device.queue,1,&source,0,nullptr,nullptr),"GL frame acquire");
            check(cl.copyImageToBuffer(device.queue,source,staging,origin,region,0,0,nullptr,nullptr),"GL frame copy");
            check(cl.releaseGL(device.queue,1,&source,0,nullptr,nullptr),"GL frame release");
            check(cl.copyBufferToImage(device.queue,staging,pq?packed->mem:input->mem,0,origin,region,0,nullptr,nullptr),"GL frame upload");
        } else {
            check(cl.writeImage(device.queue,pq?packed->mem:input->mem,CL_TRUE,origin,region,f->row_pitch,0,f->data,0,nullptr,nullptr),"capture upload");
        }
        if(pq)dispatch.call("capture_unpack_pq",size_t(f->width)*f->height,256,nullptr,packed->mem,input->mem,f->width,f->height);
        cl_mem source=input->mem;
        if(f->width!=codedWidth || f->height!=codedHeight || config.width!=codedWidth || config.height!=codedHeight) {
            if(!fitted)fitted=std::make_unique<Image>(cl,device,codedWidth,codedHeight,CL_RGBA,
                linear?CL_HALF_FLOAT:CL_UNORM_INT8);
            dispatch.call("capture_fit",size_t(codedWidth)*codedHeight,256,nullptr,input->mem,fitted->mem,codedWidth,codedHeight,config.width,config.height);
            source=fitted->mem;
        }
        bool intra=!haveFrame || requestedIdr;if(intra)poc=0;
        dispatch.zero(conversionError,4,nullptr);
        cl_uint mode=config.hdr?2:f->format==AGHL_SCRGB16F?1:0;
        dispatch.call("capture_yuv",size_t(codedWidth)*codedHeight,256,nullptr,source,codedWidth,codedHeight,config.bit_depth,mode,
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
    return aghl_probe_device(nullptr,name,n,error,capacity);
}
extern "C" AGHL_API int aghl_probe_device(const char* selector,char* name,size_t n,char* error,size_t capacity) {
    message(error,capacity,"");message(name,n,"");
    try{LinuxCL cl(selector);message(name,n,cl.name.c_str());return 0;}
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
    try{session->encode(frame,id,force!=0,*out);return 0;}
    catch(const ExternalUnavailable& e){message(error,capacity,e.what());return AGHL_EXTERNAL_UNAVAILABLE;}
    catch(const std::exception& e){message(error,capacity,e.what());}
    catch(...){message(error,capacity,"Unknown Linux GPU encode failure");}session->failed=true;return -1;
}
extern "C" AGHL_API void aghl_close(aghl_session* session){delete session;}
extern "C" AGHL_API int aghl_gl_sharing(const aghl_session* session){return session && session->glShare.active?1:0;}
