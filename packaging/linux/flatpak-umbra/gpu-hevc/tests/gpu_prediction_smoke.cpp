// SPDX-License-Identifier: GPL-2.0-or-later
// Offline comparison with x265's original C primitives. No encoder registration.
#include "opencl_test_runtime.h"
struct PredictionTrace {
    uint32_t depth,lg,chroma,width,height,count;
    std::vector<uint16_t> reference,expected;
    std::vector<int32_t> blocks;
    explicit PredictionTrace(const std::filesystem::path& path) {
        auto data=readFile(path);
        if (data.size()<32 || memcmp(data.data(),"AXPRED01",8)) throw std::runtime_error("Invalid prediction fixture");
        depth=le32(data.data()+8); lg=le32(data.data()+12); chroma=le32(data.data()+16);
        width=le32(data.data()+20); height=le32(data.data()+24); count=le32(data.data()+28);
        if ((depth!=8 && depth!=10) || lg<2 || lg>5 || chroma>1 || !count || count>1000000 ||
            width<(1u<<lg) || height<(1u<<lg) || width>8192 || height>8192) throw std::runtime_error("Invalid prediction dimensions");
        size_t samples=size_t(count)<<(2*lg);
        if (data.size()!=32+size_t(width)*height*2+size_t(count)*16+samples*2) throw std::runtime_error("Invalid prediction length");
        reference.resize(size_t(width)*height); blocks.resize(size_t(count)*4); expected.resize(samples);
        size_t at=32;
        for (auto& v:reference) { v=uint16_t(data[at]|unsigned(data[at+1])<<8); at+=2; }
        for (auto& v:blocks) { uint32_t word=le32(data.data()+at); memcpy(&v,&word,4); at+=4; }
        for (auto& v:expected) { v=uint16_t(data[at]|unsigned(data[at+1])<<8); at+=2; }
    }
};
struct PredictionDevice {
    OpenCL& cl;
    cl_context context=nullptr; cl_command_queue queue=nullptr;
    cl_program program=nullptr; cl_kernel kernel=nullptr;
    void close() {
        if (kernel) cl.clReleaseKernel(kernel);
        if (program) cl.clReleaseProgram(program);
        if (queue) cl.clReleaseCommandQueue(queue);
        if (context) cl.clReleaseContext(context);
    }
    PredictionDevice(OpenCL& api,cl_device_id id,const std::vector<uint8_t>& source,const PredictionTrace& t):cl(api) {
        try {
            cl_bool little=0; check(cl.clGetDeviceInfo(id,CL_DEVICE_ENDIAN_LITTLE,sizeof(little),&little,nullptr),"endian");
            if (!little) throw std::runtime_error("Little-endian GPU required");
            cl_int e;
            context=cl.clCreateContext(nullptr,1,&id,nullptr,nullptr,&e); check(e,"context");
            queue=cl.clCreateCommandQueue(context,id,CL_QUEUE_PROFILING_ENABLE,&e); check(e,"queue");
            const char* p=reinterpret_cast<const char*>(source.data()); size_t size=source.size();
            program=cl.clCreateProgramWithSource(context,1,&p,&size,&e); check(e,"program");
            std::string options="-cl-std=CL1.2 -DBLOCK_LOG2="+std::to_string(t.lg)+" -DPIXEL_DEPTH="+std::to_string(t.depth)+" -DCHROMA="+std::to_string(t.chroma);
            e=cl.clBuildProgram(program,1,&id,options.c_str(),nullptr,nullptr);
            if (e) {
                size_t n=0; cl.clGetProgramBuildInfo(program,id,CL_PROGRAM_BUILD_LOG,0,nullptr,&n);
                std::vector<char> log(n+1); cl.clGetProgramBuildInfo(program,id,CL_PROGRAM_BUILD_LOG,n,log.data(),nullptr);
                std::cerr<<log.data(); check(e,"kernel build");
            }
            kernel=cl.clCreateKernel(program,"inter_predict",&e); check(e,"kernel");
        } catch (...) { close(); throw; }
    }
    ~PredictionDevice() { close(); }
};
struct PredictionBuffer {
    OpenCL& cl; cl_mem mem;
    PredictionBuffer(PredictionDevice& d,size_t size,void* data):cl(d.cl) {
        cl_int e; mem=cl.clCreateBuffer(d.context,CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR,size,data,&e); check(e,"buffer");
    }
    ~PredictionBuffer() { cl.clReleaseMemObject(mem); }
    PredictionBuffer(const PredictionBuffer&)=delete;
};
static void run(PredictionDevice& d,PredictionTrace& t,const std::filesystem::path& path) {
    auto& cl=d.cl; size_t total=t.expected.size();
    std::vector<uint16_t> output(total+32,0xa55a);
    PredictionBuffer ref(d,t.reference.size()*2,t.reference.data()), blocks(d,t.blocks.size()*4,t.blocks.data());
    PredictionBuffer pred(d,output.size()*2,output.data());
    cl_uint zero=0; PredictionBuffer error(d,4,&zero);
    auto arg=[&](cl_uint i,size_t n,const void* p){check(cl.clSetKernelArg(d.kernel,i,n,p),"arg");};
    arg(0,sizeof(cl_mem),&ref.mem); arg(1,4,&t.width); arg(2,4,&t.height);
    arg(3,sizeof(cl_mem),&blocks.mem); arg(4,4,&t.count); arg(5,sizeof(cl_mem),&pred.mem); arg(6,sizeof(cl_mem),&error.mem);
    auto read=[&](PredictionBuffer& b,size_t bytes,void* p){check(cl.clEnqueueReadBuffer(d.queue,b.mem,CL_TRUE,0,bytes,p,0,nullptr,nullptr),"read");};
    auto verify=[&](size_t begin) {
        for (size_t i=begin;i<total;i++) if (output[i]!=t.expected[i]) throw std::runtime_error("Prediction mismatch sample="+std::to_string(i)+" expected="+std::to_string(t.expected[i])+" actual="+std::to_string(output[i]));
        for (size_t i=total;i<output.size();i++) if (output[i]!=0xa55a) throw std::runtime_error("Output guard damaged");
    };
    size_t group=256, global=size_t(t.count)*group;
    std::vector<double> timings,hostTimings;
    for (unsigned trial=0;trial<16;trial++) {
        auto before=std::chrono::steady_clock::now(); cl_event event;
        check(cl.clEnqueueNDRangeKernel(d.queue,d.kernel,1,nullptr,&global,&group,0,nullptr,&event),"launch");
        cl_uint status; read(error,4,&status); read(pred,output.size()*2,output.data());
        auto after=std::chrono::steady_clock::now();
        cl_ulong start,finish;
        check(cl.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(start),&start,nullptr),"start");
        check(cl.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(finish),&finish,nullptr),"finish"); cl.clReleaseEvent(event);
        if (status) throw std::runtime_error("Valid input rejected");
        verify(0);
        if (trial>=5) { timings.push_back(double(finish-start)/1e6); hostTimings.push_back(std::chrono::duration<double,std::milli>(after-before).count()); }
    }
    // Reject an out-of-contract vector before reading reference or writing its block.
    t.blocks[2]=32768;
    check(cl.clEnqueueWriteBuffer(d.queue,blocks.mem,CL_TRUE,0,t.blocks.size()*4,t.blocks.data(),0,nullptr,nullptr),"bad vector");
    size_t area=size_t(1)<<(2*t.lg); std::fill_n(output.begin(),area,uint16_t(0xa55a));
    check(cl.clEnqueueWriteBuffer(d.queue,pred.mem,CL_TRUE,0,area*2,output.data(),0,nullptr,nullptr),"negative sentinel");
    check(cl.clEnqueueNDRangeKernel(d.queue,d.kernel,1,nullptr,&global,&group,0,nullptr,nullptr),"negative launch");
    cl_uint status; read(error,4,&status); read(pred,output.size()*2,output.data());
    if (status!=1 || !std::all_of(output.begin(),output.begin()+area,[](uint16_t v){return v==0xa55a;})) throw std::runtime_error("Invalid-vector guard failed");
    verify(area);
    std::sort(timings.begin(),timings.end()); std::sort(hostTimings.begin(),hostTimings.end());
    std::cout<<"PASS trace="<<path.filename().string()<<" depth="<<t.depth<<" plane="<<(t.chroma?"chroma":"luma")<<" reference="<<t.width<<'x'<<t.height
        <<" samples="<<total<<" exact=1 guards=1 trials=11 device_ms_median="<<timings[5]<<" device_ms_max="<<timings.back()<<" submit_read_ms_median="<<hostTimings[5]<<'\n';
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        if (argc!=4) throw std::runtime_error("Usage: gpu_prediction_smoke prediction.cl fixture-directory vendor-substring");
        OpenCL cl; cl_uint n=0; check(cl.clGetPlatformIDs(0,nullptr,&n),"platform count");
        std::vector<cl_platform_id> platforms(n); check(cl.clGetPlatformIDs(n,platforms.data(),nullptr),"platforms");
        auto source=readFile(argv[1]); std::vector<std::filesystem::path> paths;
        for (const auto& entry:std::filesystem::recursive_directory_iterator(argv[2]))
            if (entry.is_regular_file() && entry.path().extension()==".prediction") paths.push_back(entry.path());
        std::sort(paths.begin(),paths.end()); if (paths.empty()) throw std::runtime_error("No fixtures");
        unsigned tested=0;
        for (auto p:platforms) {
            cl_int e=cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,0,nullptr,&n); if (e==CL_DEVICE_NOT_FOUND) continue;
            check(e,"device count"); std::vector<cl_device_id> ids(n); check(cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,n,ids.data(),nullptr),"devices");
            for (auto id:ids) {
                if (cl.info(id,CL_DEVICE_VENDOR).find(argv[3])==std::string::npos) continue;
                std::cout<<"GPU name="<<cl.info(id,CL_DEVICE_NAME)<<" driver="<<cl.info(id,CL_DRIVER_VERSION)<<'\n';
                std::map<unsigned,std::unique_ptr<PredictionDevice>> variants;
                for (const auto& path:paths) {
                    PredictionTrace t(path); unsigned key=t.depth*100+t.chroma*10+t.lg;
                    auto& d=variants[key]; if (!d) d=std::make_unique<PredictionDevice>(cl,id,source,t);
                    run(*d,t,path);
                }
                tested++;
            }
        }
        if (!tested) throw std::runtime_error("No matching GPU; no CPU fallback");
        std::cout<<"RESULT PASS tested_gpus="<<tested<<" scope=single_reference_prediction_component\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"RESULT FAIL "<<e.what()<<'\n'; return 1; }
}
