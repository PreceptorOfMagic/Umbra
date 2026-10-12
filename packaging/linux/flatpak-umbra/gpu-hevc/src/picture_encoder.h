// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Development encoder backend shared by file and GPU-surface smoke tests.
// Fixed coding profile; not yet an Apollo production encoder.
#include "cabac_backend.h"
struct PictureConfig {
    uint32_t width=0,height=0,depth=0,qp=28;
    bool adaptiveQp=false,deblock=false,isolatedIntra=false,originalMotion=false;
    std::vector<uint16_t> mvCosts;
    static PictureConfig validated(const PictureConfig& h) {
        if(h.width<64 || h.height<64 || h.width>8192 || h.height>8192 || h.width%32 || h.height%32 ||
            (h.depth!=8 && h.depth!=10) || h.qp!=28 || h.mvCosts.size()!=257)throw std::runtime_error("Unsupported picture configuration");
        return h;
    }
};
static void addKernel(Device& d,const char* name) {
    cl_int e;cl_kernel k=d.cl.clCreateKernel(d.program,name,&e);check(e,name);d.kernels[name]=k;
}
static void addProgram(Device& d,cl_device_id id,const std::vector<uint8_t>& source,const std::string& options,
                       std::initializer_list<std::pair<const char*,const char*>> kernels) {
    cl_int e;const char* p=reinterpret_cast<const char*>(source.data());size_t n=source.size();
    cl_program program=d.cl.clCreateProgramWithSource(d.context,1,&p,&n,&e);check(e,"residual program");
    e=d.cl.clBuildProgram(program,1,&id,options.c_str(),nullptr,nullptr);
    if(e) {
        size_t bytes=0;d.cl.clGetProgramBuildInfo(program,id,CL_PROGRAM_BUILD_LOG,0,nullptr,&bytes);
        std::vector<char> log(bytes+1);d.cl.clGetProgramBuildInfo(program,id,CL_PROGRAM_BUILD_LOG,bytes,log.data(),nullptr);
        std::cerr<<log.data();d.cl.clReleaseProgram(program);check(e,"frame residual build");
    }
    for(const auto& entry:kernels){
        cl_kernel k=d.cl.clCreateKernel(program,entry.second,&e);
        if(e){d.cl.clReleaseProgram(program);check(e,entry.second);}
        d.kernels[entry.first]=k;
    }
    d.cl.clReleaseProgram(program);
}
struct Prefix {
    uint32_t count;
    Buffer counts,prefix,offsets,tiles,tilePrefix,total;
    Prefix(Device& d,uint32_t n):count(n),counts(d,size_t(n)*4),prefix(d,size_t(n)*4),offsets(d,size_t(n)*4),
        tiles(d,size_t((n+255)/256)*4),tilePrefix(d,size_t((n+255)/256)*4),total(d,4){}
    void scan(ParallelCabac& p,std::vector<cl_event>& events) {
        p.call("cb_scan_tiles",count,256,&events,counts.mem,offsets.mem,tiles.mem,count,cl_uint(0));
        p.call("cb_scan_totals",256,256,&events,tiles.mem,tilePrefix.mem,(count+255)/256,cl_uint(0));
        p.call("cb_scan_add",count,256,&events,offsets.mem,tilePrefix.mem,prefix.mem,count,cl_uint(0));
        p.call("coefficient_offsets",count,256,&events,prefix.mem,offsets.mem,count,total.mem);
    }
};
struct Plane {
    uint32_t width,height,lg,blocks,groups;
    Buffer input,reference,predicted,recon,coeff,params,syntaxParams,masks,lasts,previous,diagnostics;
    Prefix prefix;
    std::unique_ptr<Buffer> ops;uint32_t capacity=1;
    Plane(Device& d,uint32_t w,uint32_t h,uint32_t log):width(w),height(h),lg(log),blocks((w>>log)*(h>>log)),groups(blocks*(1u<<(2*log-4))),
        input(d,size_t(w)*h*2),reference(d,size_t(w)*h*2),predicted(d,size_t(w)*h*2),recon(d,size_t(w)*h*2),coeff(d,size_t(w)*h*2),params(d,size_t(blocks)*16),
        syntaxParams(d,size_t(blocks)*16),masks(d,size_t(blocks)*8),lasts(d,size_t(blocks)*4),previous(d,size_t(groups)*4),diagnostics(d,4),prefix(d,groups),ops(new Buffer(d,16)){}
};

struct EncodedPicture {
    std::vector<uint8_t> bytes;
    uint32_t operations;
    double deviceSpanMs,submitReadMs;
    std::map<std::string,double> stages;
};
struct GpuPictureEncoder {
    Device& d;
    OpenCL& cl;
    const PictureConfig h;
    ParallelCabac dispatch;
    std::array<std::unique_ptr<Plane>,3> planes;
    uint32_t blocks;
    Prefix picture;
    Buffer errors,initial,finalState,result,vectors,priorVectors,chromaVectors,mvCosts,cuQps,qpDeltas,intraModes;
    uint32_t outputCapacity=16,opCapacity=0;
    std::unique_ptr<Buffer> output,pictureOps,motionReference;
    std::unique_ptr<ParallelCabac> encoder;
    bool haveReference=false;
    GpuPictureEncoder(Device& device,const PictureConfig& config):d(device),cl(d.cl),h(PictureConfig::validated(config)),dispatch(d,1,1),
        blocks((h.width/32)*(h.height/32)),picture(d,blocks),errors(d,4),initial(d,160),finalState(d,160),result(d,12),
        vectors(d,size_t(blocks)*16),priorVectors(d,size_t(blocks)*16),chromaVectors(d,size_t(blocks)*16),
        mvCosts(d,h.mvCosts.size()*2,const_cast<uint16_t*>(h.mvCosts.data())),cuQps(d,size_t(blocks)*4),qpDeltas(d,size_t(blocks)*4),intraModes(d,size_t(blocks)*4),output(std::make_unique<Buffer>(d,16)) {
        if(h.originalMotion)motionReference=std::make_unique<Buffer>(d,size_t(h.width)*h.height*2);
        for(unsigned c=0;c<3;c++){
            planes[c]=std::make_unique<Plane>(d,h.width>>(c?1:0),h.height>>(c?1:0),c?4:5);
            std::vector<uint32_t> syntax(size_t(blocks)*4);
            for(uint32_t b=0;b<blocks;b++)syntax[b*4]=c?0:1;
            write(planes[c]->syntaxParams,syntax.size()*4,syntax.data());
        }
        dispatch.zero(priorVectors,size_t(blocks)*16,nullptr);dispatch.zero(vectors,size_t(blocks)*16,nullptr);
    }
    void read(Buffer& b,size_t bytes,void* p){check(cl.clEnqueueReadBuffer(d.queue,b.mem,CL_TRUE,0,bytes,p,0,nullptr,nullptr),"read");}
    void write(Buffer& b,size_t bytes,const void* p){check(cl.clEnqueueWriteBuffer(d.queue,b.mem,CL_TRUE,0,bytes,p,0,nullptr,nullptr),"write");}
    // Surface adapters may write directly to planes[c]->input on d.queue.
    void upload(unsigned c,const std::vector<uint16_t>& pixels) {
        auto& p=*planes.at(c);
        if(pixels.size()!=size_t(p.width)*p.height)throw std::runtime_error("Invalid plane dimensions");
        write(p.input,pixels.size()*2,pixels.data());
    }
    std::vector<uint16_t> reconstruction(unsigned c) {
        if(!haveReference)throw std::runtime_error("No reconstructed picture");
        auto& p=*planes.at(c);std::vector<uint16_t> pixels(size_t(p.width)*p.height);
        read(p.reference,pixels.size()*2,pixels.data());return pixels;
    }
    void auditMotion(uint32_t frame) {
            std::vector<int32_t> motion(size_t(blocks)*4);read(priorVectors,motion.size()*4,motion.data());
            unsigned moving=0,fractional=0,diagonalFractional=0,maximum=0;
            for(uint32_t b=0;b<blocks;b++){
                int x=motion[b*4+2],y=motion[b*4+3];moving+=(x!=0 || y!=0);
                maximum=std::max(maximum,unsigned(std::max(std::abs(x),std::abs(y))));
                fractional+=(x%4 || y%4);
                diagonalFractional+=((x&1) && (y&1));
                int ox=motion[b*4],oy=motion[b*4+1],rx=ox+(x>>2),ry=oy+(y>>2),cx=ox/2+(x>>3),cy=oy/2+(y>>3);
                if(rx<0 || ry<0 || rx+32>int(h.width) || ry+32>int(h.height) ||
                    ((x&3) && (rx<3 || rx+36>int(h.width))) || ((y&3) && (ry<3 || ry+36>int(h.height))) ||
                    ((x&7) && (cx<1 || cx+18>int(h.width/2))) || ((y&7) && (cy<1 || cy+18>int(h.height/2))))
                    throw std::runtime_error("Motion filter footprint escapes the pane");
            }
            std::cout<<"MOTION frame="<<frame<<" nonzero_blocks="<<moving<<" fractional_blocks="<<fractional<<" diagonal_fractional_blocks="<<diagonalFractional<<" total_blocks="<<blocks<<" max_quarterpel="<<maximum<<'\n';
    }
    void commitPicture() {
        if(h.originalMotion)check(cl.clEnqueueCopyBuffer(d.queue,planes[0]->input.mem,motionReference->mem,0,0,size_t(h.width)*h.height*2,0,nullptr,nullptr),"retain motion source");
        haveReference=true;
        for(auto& p:planes)std::swap(p->reference.mem,p->recon.mem);
        std::swap(priorVectors.mem,vectors.mem);
    }
    EncodedPicture encode(bool isIntra,const uint8_t* contexts,uint32_t qp=28,bool commit=true,uint32_t aqPressure=0,bool reusePrediction=false,uint32_t aqFraction=0,uint32_t qpPhase=0) {
        if(qp>51)throw std::runtime_error("Picture QP outside 0..51");
        if(!isIntra && !haveReference)throw std::runtime_error("Predicted picture before an intra reference");
        uint32_t intra=isIntra?1:0;
        auto start=std::chrono::steady_clock::now();
        write(initial,160,contexts);
        std::vector<cl_event> events;dispatch.labels.clear();
        dispatch.zero(errors,4,&events);
        if(h.adaptiveQp)dispatch.call("picture_quality_qp",size_t(blocks)*256,256,&events,
            planes[0]->input.mem,h.width,h.height,h.depth,qp,aqPressure,aqFraction,qpPhase,cuQps.mem);
        if(!intra && !reusePrediction){
            dispatch.call("motion_search",size_t(blocks)*256,256,&events,planes[0]->input.mem,planes[0]->reference.mem,h.originalMotion?motionReference->mem:planes[0]->reference.mem,
                h.width,h.height,priorVectors.mem,vectors.mem,mvCosts.mem,cl_uint(32));
            dispatch.call("chroma_vectors",blocks,256,&events,vectors.mem,chromaVectors.mem,blocks);
        }
        for(unsigned c=0;c<3;c++) {
            auto& p=*planes[c];const char* kernel=c?"frame_chroma":"frame_luma";
            if(!intra && !reusePrediction)dispatch.call(c?"predict_chroma":"predict_luma",size_t(blocks)*256,256,&events,
                p.reference.mem,p.width,p.height,c?chromaVectors.mem:vectors.mem,blocks,p.predicted.mem,errors.mem);
            // x265 common/quant.cpp setChromaQP + constants.cpp g_chromaScale
            // at pinned b81f650: 4:2:0 chroma QP becomes non-linear above 29.
            static constexpr uint32_t chromaScale[]={29,30,31,32,33,33,34,34,35,35,36,36,37,37,38,39,40,41,42,43,44,45};
            const uint32_t planeQp=c && qp>=30?chromaScale[qp-30]:qp;
            uint32_t diagonals=intra?((p.width>>p.lg)+(p.height>>p.lg)-1):1;
            for(uint32_t diag=0;diag<diagonals;diag++)dispatch.call(kernel,size_t(intra?(p.width>>p.lg):blocks)*256,256,&events,
                p.input.mem,p.predicted.mem,p.params.mem,blocks,p.lg,h.depth,cl_uint(0),p.coeff.mem,p.recon.mem,p.diagnostics.mem,errors.mem,
                p.width,p.height,(!intra && h.isolatedIntra)?(0x40000000u|(qpPhase&3)):diag,intra,planeQp,cuQps.mem,cl_uint(h.adaptiveQp),intraModes.mem,cl_uint(0));
            // Reconstruct the three non-eligible grid positions first. The remaining
            // position then chooses intra/inter using immutable neighbor samples.
            // Every CU transforms once, including on quantization retries.
            if(!c && h.isolatedIntra)dispatch.call("picture_select_intra",size_t(blocks)*256,256,&events,
                p.input.mem,p.predicted.mem,p.recon.mem,h.width,h.height,h.depth,intra,qpPhase,intraModes.mem);
            if(!intra && h.isolatedIntra)dispatch.call(kernel,size_t(blocks)*256,256,&events,
                p.input.mem,p.predicted.mem,p.params.mem,blocks,p.lg,h.depth,cl_uint(0),p.coeff.mem,p.recon.mem,p.diagnostics.mem,errors.mem,
                p.width,p.height,cl_uint(0x80000000u|(qpPhase&3)),intra,planeQp,cuQps.mem,cl_uint(h.adaptiveQp),intraModes.mem,cl_uint(1));
            dispatch.call("coefficient_metadata",size_t(blocks)*64,64,&events,p.coeff.mem,p.syntaxParams.mem,blocks,p.lg,p.masks.mem,p.lasts.mem,p.previous.mem,errors.mem);
            dispatch.call("coefficient_bins",p.groups,64,&events,p.coeff.mem,p.syntaxParams.mem,blocks,p.lg,cl_uint(0),p.prefix.offsets.mem,p.prefix.counts.mem,p.ops->mem,p.capacity,errors.mem,p.masks.mem,p.lasts.mem,p.previous.mem);
            p.prefix.scan(dispatch,events);
            uint32_t count;read(p.prefix.total,4,&count);count--; // no component terminator; picture supplies it
            if(count>p.capacity){p.capacity=count;p.ops=std::make_unique<Buffer>(d,size_t(count)*16);}
            dispatch.call("coefficient_bins",p.groups,64,&events,p.coeff.mem,p.syntaxParams.mem,blocks,p.lg,cl_uint(1),p.prefix.offsets.mem,p.prefix.counts.mem,p.ops->mem,p.capacity,errors.mem,p.masks.mem,p.lasts.mem,p.previous.mem);
        }
        if(h.adaptiveQp)dispatch.call("picture_qp_deltas",1,1,&events,blocks,h.depth,qp,cuQps.mem,
            planes[0]->lasts.mem,planes[1]->lasts.mem,planes[2]->lasts.mem,qpDeltas.mem);
        if(h.deblock)for(unsigned axis=0;axis<2;axis++)for(unsigned c=0;c<3;c++) {
            if(c && !intra && !h.isolatedIntra)continue;
            auto& p=*planes[c];
            dispatch.call("picture_deblock",size_t(blocks)*(c?4:8),256,&events,p.recon.mem,p.width,p.height,h.depth,
                cl_uint(c!=0),intra,cl_uint(axis),qp,cl_uint(h.adaptiveQp),cuQps.mem,planes[0]->lasts.mem,vectors.mem,intraModes.mem,cl_uint(h.isolatedIntra));
        }
        auto pictureCall=[&](uint32_t emit){dispatch.call("picture_bins",size_t(blocks)*64,64,&events,blocks,intra,emit,
            planes[0]->prefix.prefix.mem,planes[1]->prefix.prefix.mem,planes[2]->prefix.prefix.mem,
            planes[0]->ops->mem,planes[1]->ops->mem,planes[2]->ops->mem,picture.offsets.mem,picture.counts.mem,
            pictureOps?pictureOps->mem:output->mem,opCapacity,errors.mem,vectors.mem,h.width/32,qpDeltas.mem,cl_uint(h.adaptiveQp),intraModes.mem,cl_uint(h.isolatedIntra));};
        pictureCall(0);picture.scan(dispatch,events);
        uint32_t count,error;read(picture.total,4,&count);count--;read(errors,4,&error);
        if(error || !count)throw std::runtime_error("Picture generation error="+std::to_string(error));
        // A context/termination op shifts at most 7 bits; bypass ops at most 32.
        // Four bytes per operation plus final alignment/carry is a conservative
        // bound independent of source pixels or the oracle's compressed size.
        // Reuse high-water allocations; scan only this frame's bounded capacity.
        if(count>200000000u)throw std::runtime_error("Picture syntax exceeds supported allocation bound");
        if(count>opCapacity){
            opCapacity=count+count/8+256;
            pictureOps=std::make_unique<Buffer>(d,size_t(opCapacity)*16);
            output=std::make_unique<Buffer>(d,size_t(opCapacity)*4+32);
            encoder=std::make_unique<ParallelCabac>(d,opCapacity,opCapacity*4+32);
        }
        outputCapacity=count*4+32;
        encoder->digitsCount=outputCapacity+8;
        pictureCall(1);auto labels=dispatch.labels;
        encoder->launch(count,outputCapacity,0,*pictureOps,initial,*output,finalState,result,&events);
        labels.insert(labels.end(),encoder->labels.begin(),encoder->labels.end());
        std::array<uint32_t,3> status;read(result,12,status.data());read(errors,4,&error);
        if(error || status[1] || !status[0] || status[0]>outputCapacity)throw std::runtime_error("Picture CABAC error="+std::to_string(status[1])+" syntax="+std::to_string(error));
        std::vector<uint8_t> coded(status[0]);read(*output,coded.size(),coded.data());
        auto finish=std::chrono::steady_clock::now();
        std::map<std::string,double> stages;cl_ulong first=0,last=0;
        for(size_t i=0;i<events.size();i++){
            cl_ulong a,b;check(cl.clGetEventProfilingInfo(events[i],CL_PROFILING_COMMAND_START,sizeof(a),&a,nullptr),"event start");
            check(cl.clGetEventProfilingInfo(events[i],CL_PROFILING_COMMAND_END,sizeof(b),&b,nullptr),"event finish");
            if(!i)first=a;
            last=b;stages[labels[i]]+=double(b-a)/1e6;cl.clReleaseEvent(events[i]);
        }
        if(commit)commitPicture();
        return {std::move(coded),count,double(last-first)/1e6,
            std::chrono::duration<double,std::milli>(finish-start).count(),std::move(stages)};
    }
};

static std::vector<uint8_t> pictureProgram(const std::filesystem::path& dir) {
    auto source=readFile(dir/"cabac_parallel.cl");
    for(const char* f:{"coefficients.cl","picture.cl"}){auto text=readFile(dir/f);source.push_back('\n');source.insert(source.end(),text.begin(),text.end());}
    return source;
}
static void initPictureKernels(Device& d,cl_device_id id,const std::filesystem::path& dir,uint32_t depth) {
    auto residual=readFile(dir/"residual.cl");
    auto prediction=readFile(dir/"prediction.cl"),motion=readFile(dir/"motion.cl"),predictionAndMotion=prediction;
    predictionAndMotion.push_back('\n');predictionAndMotion.insert(predictionAndMotion.end(),motion.begin(),motion.end());
                for(const char* name:{"coefficient_metadata","coefficient_bins","coefficient_offsets","picture_bins","picture_quality_qp","picture_qp_deltas","picture_deblock","picture_select_intra"})addKernel(d,name);
                std::string options="-cl-std=CL1.2 -DPIXEL_DEPTH="+std::to_string(depth);
                addProgram(d,id,residual,options+" -DFRAME_PREDICTION -DTU_LOG2=5",{{"frame_luma","residual_encode"}});
                addProgram(d,id,residual,options+" -DFRAME_PREDICTION -DTU_LOG2=4",{{"frame_chroma","residual_encode"}});
                addProgram(d,id,predictionAndMotion,options+" -DBLOCK_LOG2=5 -DCHROMA=0",{{"predict_luma","inter_predict"},{"motion_search","motion_search"},{"chroma_vectors","chroma_vectors"}});
                addProgram(d,id,prediction,options+" -DBLOCK_LOG2=4 -DCHROMA=1",{{"predict_chroma","inter_predict"}});

}
