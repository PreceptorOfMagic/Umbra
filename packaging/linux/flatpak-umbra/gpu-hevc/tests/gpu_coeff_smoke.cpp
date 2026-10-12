// SPDX-License-Identifier: GPL-2.0-or-later
// GPU coefficient syntax -> GPU CABAC, checked against the pinned x265 oracle.
#include "cabac_test_backend.h"
#include "residual_trace.h"
struct CoeffTrace {
    uint32_t lg,blocks;
    std::vector<uint32_t> parameters;
    std::vector<int16_t> coefficients;
    explicit CoeffTrace(const std::filesystem::path& path) {
        auto bytes=readFile(path);
        if(bytes.size()<16 || memcmp(bytes.data(),"AXCOEF01",8)) throw std::runtime_error("Invalid coefficient header");
        lg=le32(bytes.data()+8); blocks=le32(bytes.data()+12);
        if(lg<2 || lg>5 || !blocks || blocks>65536) throw std::runtime_error("Invalid coefficient dimensions");
        size_t area=size_t(1)<<(2*lg),at=16;
        if(bytes.size()!=16+size_t(blocks)*(16+2*area)) throw std::runtime_error("Coefficient size mismatch");
        parameters.resize(size_t(blocks)*4); coefficients.resize(size_t(blocks)*area);
        for(uint32_t b=0;b<blocks;b++) {
            for(unsigned j=0;j<4;j++,at+=4) parameters[size_t(b)*4+j]=le32(bytes.data()+at);
            for(size_t j=0;j<area;j++,at+=2) coefficients[size_t(b)*area+j]=int16_t(bytes[at]|unsigned(bytes[at+1])<<8);
        }
    }
};
static void runCoefficients(Device& d,const std::filesystem::path& path, ResidualTrace* raw) {
    CoeffTrace t(path);
    auto referencePath=path; referencePath.replace_extension(".cabac");
    Trace reference(referencePath);
    cl_uint expectedCount=cl_uint(reference.ops.size()/4), capacity=expectedCount+64;
    cl_uint outputCapacity=cl_uint(reference.expected.size()+64);
    if(raw && (raw->lg!=t.lg || raw->blocks!=t.blocks)) throw std::runtime_error("Residual/syntax dimensions differ");
    Buffer coefficients(d,t.coefficients.size()*2,raw?nullptr:t.coefficients.data()), parameters(d,t.parameters.size()*4,t.parameters.data());
    std::unique_ptr<Buffer> sourcePixels,predictedPixels,residualParameters,reconstructed,diagnostics;
    if(raw) {
        size_t bytes=raw->planes[0].size()*2;
        sourcePixels=std::make_unique<Buffer>(d,bytes,raw->planes[0].data());
        predictedPixels=std::make_unique<Buffer>(d,bytes,raw->planes[1].data());
        residualParameters=std::make_unique<Buffer>(d,raw->parameters.size()*4,raw->parameters.data());
        reconstructed=std::make_unique<Buffer>(d,bytes);
        diagnostics=std::make_unique<Buffer>(d,4); // debug stores disabled in integrated mode
    }
    cl_uint groups=t.blocks*(1u<<(2*t.lg-4));
    Buffer counts(d,size_t(groups)*4), offsets(d,size_t(groups)*4), total(d,4), errors(d,4);
    Buffer masks(d,size_t(t.blocks)*8),lasts(d,size_t(t.blocks)*4),previous(d,size_t(groups)*4);
    Buffer sumTiles(d,size_t((groups+255)/256)*4),sumTilePrefix(d,size_t((groups+255)/256)*4);
    std::vector<cl_uint> generated(size_t(capacity)*4,0xa5a5a5a5);
    Buffer ops(d,generated.size()*4,generated.data()), initial(d,160,reference.initial.data());
    std::vector<uint8_t> bytes(outputCapacity+16,0xa5);
    Buffer output(d,bytes.size(),bytes.data()), finalState(d,160), result(d,12);
    ParallelCabac pipeline(d,capacity,outputCapacity);
    auto read=[&](Buffer& b,size_t n,void* p){check(d.cl.clEnqueueReadBuffer(d.queue,b.mem,CL_TRUE,0,n,p,0,nullptr,nullptr),"readback");};
    std::vector<double> deviceTimes,hostTimes;
    for(unsigned trial=0;trial<16;trial++) {
        std::vector<cl_event> events;
        pipeline.labels.clear();
        auto start=std::chrono::steady_clock::now();
        pipeline.zero(errors,4,&events);
        if(raw) pipeline.call("residual_encode",size_t(t.blocks)*256,256,&events,sourcePixels->mem,predictedPixels->mem,residualParameters->mem,
            t.blocks,t.lg,raw->depth,cl_uint(0),coefficients.mem,reconstructed->mem,diagnostics->mem,errors.mem);
        pipeline.call("coefficient_metadata",size_t(t.blocks)*64,64,&events,coefficients.mem,parameters.mem,t.blocks,t.lg,masks.mem,lasts.mem,previous.mem,errors.mem);
        pipeline.call("coefficient_bins",groups,64,&events,coefficients.mem,parameters.mem,t.blocks,t.lg,cl_uint(0),offsets.mem,counts.mem,ops.mem,capacity,errors.mem,masks.mem,lasts.mem,previous.mem);
        pipeline.call("cb_scan_tiles",groups,256,&events,counts.mem,offsets.mem,sumTiles.mem,groups,cl_uint(0));
        pipeline.call("cb_scan_totals",256,256,&events,sumTiles.mem,sumTilePrefix.mem,(groups+255)/256,cl_uint(0));
        pipeline.call("cb_scan_add",groups,256,&events,offsets.mem,sumTilePrefix.mem,counts.mem,groups,cl_uint(0));
        pipeline.call("coefficient_offsets",groups,256,&events,counts.mem,offsets.mem,groups,total.mem);
        pipeline.call("coefficient_bins",groups,64,&events,coefficients.mem,parameters.mem,t.blocks,t.lg,cl_uint(1),offsets.mem,counts.mem,ops.mem,capacity,errors.mem,masks.mem,lasts.mem,previous.mem);
        pipeline.call("coefficient_finish",1,1,&events,total.mem,ops.mem,capacity,errors.mem);
        cl_uint count=0,error=0; read(total,4,&count); read(errors,4,&error);
        if(error || count!=expectedCount) throw std::runtime_error("Syntax generation error="+std::to_string(error)+" count="+std::to_string(count)+" expected="+std::to_string(expectedCount));
        auto labels=pipeline.labels;
        // Pass the GPU-generated operations directly into CABAC; no bin upload.
        pipeline.launch(count,outputCapacity,0,ops,initial,output,finalState,result,&events);
        labels.insert(labels.end(),pipeline.labels.begin(),pipeline.labels.end());
        std::array<cl_uint,3> status; read(result,12,status.data()); read(output,bytes.size(),bytes.data());
        auto finish=std::chrono::steady_clock::now();
        cl_ulong a=0,b=0;
        check(d.cl.clGetEventProfilingInfo(events.front(),CL_PROFILING_COMMAND_START,sizeof(a),&a,nullptr),"start");
        check(d.cl.clGetEventProfilingInfo(events.back(),CL_PROFILING_COMMAND_END,sizeof(b),&b,nullptr),"end");
        std::map<std::string,double> stages;
        for(size_t i=0;i<events.size();i++) {
            if(trial==5) {
                cl_ulong s=0,f=0;
                check(d.cl.clGetEventProfilingInfo(events[i],CL_PROFILING_COMMAND_START,sizeof(s),&s,nullptr),"stage start");
                check(d.cl.clGetEventProfilingInfo(events[i],CL_PROFILING_COMMAND_END,sizeof(f),&f,nullptr),"stage end");
                stages[labels[i]]+=double(f-s)/1e6;
            }
            d.cl.clReleaseEvent(events[i]);
        }
        for(const auto& stage:stages) std::cout << "STAGE " << stage.first << " ms=" << stage.second << '\n';
        if(status[1] || status[0]!=reference.expected.size()) throw std::runtime_error("CABAC rejected generated syntax");
        if(!std::equal(reference.expected.begin(),reference.expected.end(),bytes.begin())) {
            read(ops,generated.size()*4,generated.data());
            for(size_t i=0;i<size_t(count)*4;i++) if(i%4!=3 && generated[i]!=reference.ops[i])
                throw std::runtime_error("Bin mismatch op="+std::to_string(i/4)+" field="+std::to_string(i%4)+" got="+std::to_string(generated[i])+" expected="+std::to_string(reference.ops[i]));
            throw std::runtime_error("CABAC byte mismatch despite matching bins");
        }
        std::array<uint8_t,160> contexts; read(finalState,160,contexts.data());
        if(contexts!=reference.finalState) throw std::runtime_error("Final contexts mismatch");
        if(!std::all_of(bytes.begin()+reference.expected.size(),bytes.end(),[](uint8_t v){return v==0xa5;})) throw std::runtime_error("CABAC output guard damaged");
        if(trial==0) {
            if(raw) {
                std::vector<uint16_t> pixels(raw->planes[0].size());
                read(coefficients,pixels.size()*2,pixels.data());
                if(pixels!=raw->planes[4]) throw std::runtime_error("Integrated quantized coefficients mismatch");
                read(*reconstructed,pixels.size()*2,pixels.data());
                if(pixels!=raw->planes[7]) throw std::runtime_error("Integrated reconstructed pixels mismatch");
            }
            read(ops,generated.size()*4,generated.data());
            for(size_t i=0;i<size_t(count)*4;i++) if(i%4!=3 && generated[i]!=reference.ops[i]) throw std::runtime_error("Generated bins mismatch");
            if(!std::all_of(generated.begin()+size_t(count)*4,generated.end(),[](cl_uint v){return v==0xa5a5a5a5;})) throw std::runtime_error("Syntax output guard damaged");
        }
        if(trial>=5) { deviceTimes.push_back(double(b-a)/1e6); hostTimes.push_back(std::chrono::duration<double,std::milli>(finish-start).count()); }
    }
    // Zero output capacity is rejected; generated syntax must stay unchanged.
    pipeline.zero(errors,4,nullptr);
    pipeline.call("coefficient_bins",groups,64,nullptr,coefficients.mem,parameters.mem,t.blocks,t.lg,cl_uint(1),offsets.mem,counts.mem,ops.mem,cl_uint(0),errors.mem,masks.mem,lasts.mem,previous.mem);
    cl_uint error; read(errors,4,&error);
    if(error!=1) throw std::runtime_error("Syntax capacity guard failed");
    read(ops,generated.size()*4,generated.data());
    for(size_t i=0;i<size_t(expectedCount)*4;i++) if(i%4!=3 && generated[i]!=reference.ops[i]) throw std::runtime_error("Capacity rejection modified syntax");
    std::sort(deviceTimes.begin(),deviceTimes.end()); std::sort(hostTimes.begin(),hostTimes.end());
    std::cout << "PASS trace=" << path.filename().string() << " TU=" << (1u<<t.lg) << " blocks=" << t.blocks
        << " raw_residual=" << (raw?1:0)
        << " ops=" << expectedCount << " bytes=" << reference.expected.size() << " exact_bins=1 exact_bytes=1 exact_contexts=1 guards=1 trials=11"
        << " device_span_ms_median=" << deviceTimes[5] << " device_span_ms_max=" << deviceTimes.back()
        << " submit_read_ms_median=" << hostTimes[5] << " submit_read_ms_max=" << hostTimes.back() << '\n';
}
int main(int argc,char** argv) {
    try {
        std::cout << std::unitbuf;
        if(argc!=5 && argc!=6) throw std::runtime_error("Usage: gpu_coeff_smoke cabac_parallel.cl coefficients.cl fixtures vendor-substring [residual.cl]");
        auto source=readFile(argv[1]), syntax=readFile(argv[2]); source.push_back('\n'); source.insert(source.end(),syntax.begin(),syntax.end());
        if(argc==6) { auto pixels=readFile(argv[5]);source.push_back('\n');source.insert(source.end(),pixels.begin(),pixels.end()); }
        OpenCL cl; cl_uint count=0; check(cl.clGetPlatformIDs(0,nullptr,&count),"platform count");
        std::vector<cl_platform_id> platforms(count); check(cl.clGetPlatformIDs(count,platforms.data(),nullptr),"platforms");
        unsigned tested=0;
        for(auto p:platforms) {
            cl_uint n=0; cl_int e=cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,0,nullptr,&n); if(e==CL_DEVICE_NOT_FOUND) continue;
            check(e,"device count"); std::vector<cl_device_id> devices(n); check(cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,n,devices.data(),nullptr),"devices");
            for(auto id:devices) {
                if(cl.info(id,CL_DEVICE_VENDOR).find(argv[4])==std::string::npos) continue;
                std::cout << "GPU name=" << cl.info(id,CL_DEVICE_NAME) << " driver=" << cl.info(id,CL_DRIVER_VERSION) << '\n';
                std::vector<std::filesystem::path> paths;
                for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[3]))
                    if(entry.is_regular_file() && entry.path().extension()==".coeff") paths.push_back(entry.path());
                std::sort(paths.begin(),paths.end()); if(paths.empty()) throw std::runtime_error("No coefficient fixtures");
                for(const auto& path:paths) {
                    std::cout << "CASE " << path.string() << '\n';
                    std::unique_ptr<ResidualTrace> raw;
                    std::string options="-cl-std=CL1.2";
                    if(argc==6) {
                        auto rawPath=path;rawPath.replace_extension(".residual");
                        raw=std::make_unique<ResidualTrace>(rawPath);
                        options+=" -DTU_LOG2="+std::to_string(raw->lg)+" -DPIXEL_DEPTH="+std::to_string(raw->depth);
                    }
                    Device d(cl,id,source,true,options);
                    for(const char* name:{"coefficient_metadata","coefficient_bins","coefficient_offsets","coefficient_finish","residual_encode"}) {
                        if(!raw && std::string(name)=="residual_encode") continue;
                        cl_int error;cl_kernel k=cl.clCreateKernel(d.program,name,&error);check(error,name);d.kernels[name]=k;
                    }
                    runCoefficients(d,path,raw.get());
                }
                tested++;
            }
        }
        if(!tested) throw std::runtime_error("No matching GPU; no CPU fallback");
        std::cout << "RESULT PASS tested_gpus=" << tested << " scope=" << (argc==6?"residual_syntax_and_CABAC_components":"coefficient_syntax_and_CABAC_only") << '\n';
        return 0;
    } catch(const std::exception& e) { std::cerr << "RESULT FAIL " << e.what() << '\n'; return 1; }
}
