// SPDX-License-Identifier: GPL-2.0-or-later
// Offline component correctness/performance test; never registers an encoder.
#include "opencl_test_runtime.h"

#include "residual_trace.h"

struct ResidualDevice {
    OpenCL& cl;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    void close() {
        if (kernel) cl.clReleaseKernel(kernel);
        if (program) cl.clReleaseProgram(program);
        if (queue) cl.clReleaseCommandQueue(queue);
        if (context) cl.clReleaseContext(context);
        kernel=nullptr; program=nullptr; queue=nullptr; context=nullptr;
    }
    ResidualDevice(OpenCL& api, cl_device_id device, const std::vector<uint8_t>& source, uint32_t lg, uint32_t depth) : cl(api) {
        try {
            cl_bool little;
            check(cl.clGetDeviceInfo(device, CL_DEVICE_ENDIAN_LITTLE, sizeof(little), &little, nullptr), "endian");
            if (!little) throw std::runtime_error("Little-endian GPU required");
            cl_int status;
            context=cl.clCreateContext(nullptr,1,&device,nullptr,nullptr,&status); check(status,"context");
            queue=cl.clCreateCommandQueue(context,device,CL_QUEUE_PROFILING_ENABLE,&status); check(status,"queue");
            const char* text=reinterpret_cast<const char*>(source.data()); size_t n=source.size();
            program=cl.clCreateProgramWithSource(context,1,&text,&n,&status); check(status,"program");
            std::string options="-cl-std=CL1.2 -DTU_LOG2="+std::to_string(lg)+" -DPIXEL_DEPTH="+std::to_string(depth);
            status=cl.clBuildProgram(program,1,&device,options.c_str(),nullptr,nullptr);
            if (status) {
                size_t len=0; cl.clGetProgramBuildInfo(program,device,CL_PROGRAM_BUILD_LOG,0,nullptr,&len);
                std::vector<char> log(len+1); cl.clGetProgramBuildInfo(program,device,CL_PROGRAM_BUILD_LOG,len,log.data(),nullptr);
                std::cerr << log.data(); check(status,"kernel build");
            }
            kernel=cl.clCreateKernel(program,"residual_encode",&status); check(status,"kernel");
        } catch (...) { close(); throw; }
    }
    ~ResidualDevice() { close(); }
};
struct ResidualBuffer {
    OpenCL& cl; cl_mem mem;
    ResidualBuffer(ResidualDevice& d,size_t size,void* data=nullptr) : cl(d.cl) {
        cl_int status;
        mem=cl.clCreateBuffer(d.context,CL_MEM_READ_WRITE|(data?CL_MEM_COPY_HOST_PTR:0),size,data,&status);
        check(status,"buffer");
    }
    ~ResidualBuffer() { cl.clReleaseMemObject(mem); }
    ResidualBuffer(const ResidualBuffer&)=delete;
};
static void run(ResidualDevice& d,const std::filesystem::path& path) {
    ResidualTrace t(path);
    auto& cl=d.cl;
    size_t total=t.planes[0].size(), bytes=total*2;
    std::vector<uint16_t> q(total+32,0xa55a), rec(q), debug(4*total+32,0xa55a);
    ResidualBuffer input(d,bytes,t.planes[0].data()), prediction(d,bytes,t.planes[1].data());
    ResidualBuffer params(d,t.parameters.size()*4,t.parameters.data());
    ResidualBuffer quant(d,q.size()*2,q.data()), recon(d,rec.size()*2,rec.data()), diagnostics(d,debug.size()*2,debug.data());
    cl_uint zero=0; ResidualBuffer error(d,4,&zero);
    auto arg=[&](cl_uint i,size_t n,const void* p){check(cl.clSetKernelArg(d.kernel,i,n,p),"argument");};
    arg(0,sizeof(cl_mem),&input.mem); arg(1,sizeof(cl_mem),&prediction.mem); arg(2,sizeof(cl_mem),&params.mem);
    arg(3,4,&t.blocks); arg(4,4,&t.lg); arg(5,4,&t.depth);
    arg(7,sizeof(cl_mem),&quant.mem); arg(8,sizeof(cl_mem),&recon.mem);
    arg(9,sizeof(cl_mem),&diagnostics.mem); arg(10,sizeof(cl_mem),&error.mem);
    auto read=[&](ResidualBuffer& b,size_t n,void* p){check(cl.clEnqueueReadBuffer(d.queue,b.mem,CL_TRUE,0,n,p,0,nullptr,nullptr),"readback");};
    auto equal=[&](const std::vector<uint16_t>& expect,const uint16_t* got,const char* stage){
        auto at=std::mismatch(expect.begin(),expect.end(),got).first-expect.begin();
        if (size_t(at)!=expect.size()) throw std::runtime_error(std::string(stage)+" mismatch sample="+std::to_string(at)+" expected="+std::to_string(expect[size_t(at)])+" actual="+std::to_string(got[at]));
    };
    std::vector<double> deviceTimes,hostTimes;
    size_t group=256,global=size_t(t.blocks)*group;
    for (unsigned repeat=0;repeat<16;repeat++) {
        cl_uint validate=repeat==0; arg(6,4,&validate);
        cl_event event;
        auto begin=std::chrono::steady_clock::now();
        check(cl.clEnqueueNDRangeKernel(d.queue,d.kernel,1,nullptr,&global,&group,0,nullptr,&event),"launch");
        cl_uint status; read(error,4,&status); read(quant,q.size()*2,q.data()); read(recon,rec.size()*2,rec.data());
        auto end=std::chrono::steady_clock::now();
        cl_ulong start=0,finish=0;
        check(cl.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_START,sizeof(start),&start,nullptr),"start");
        check(cl.clGetEventProfilingInfo(event,CL_PROFILING_COMMAND_END,sizeof(finish),&finish,nullptr),"end");
        cl.clReleaseEvent(event);
        if (status) throw std::runtime_error("Kernel rejected input flags="+std::to_string(status));
        equal(t.planes[4],q.data(),"quantized"); equal(t.planes[7],rec.data(),"reconstruction");
        if (validate) {
            read(diagnostics,debug.size()*2,debug.data());
            equal(t.planes[2],debug.data(),"residual"); equal(t.planes[3],debug.data()+total,"DCT/DST");
            equal(t.planes[5],debug.data()+2*total,"dequantized"); equal(t.planes[6],debug.data()+3*total,"inverse");
        }
        for (size_t i=0;i<32;i++) if (q[total+i]!=0xa55a || rec[total+i]!=0xa55a || debug[4*total+i]!=0xa55a)
            throw std::runtime_error("Output guard damaged");
        if (repeat>=5) {
            deviceTimes.push_back(double(finish-start)/1e6);
            hostTimes.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        }
    }
    // An invalid depth must fail without touching any output.
    cl_uint badDepth=9; arg(5,4,&badDepth);
    check(cl.clEnqueueNDRangeKernel(d.queue,d.kernel,1,nullptr,&global,&group,0,nullptr,nullptr),"negative launch");
    cl_uint status; read(error,4,&status);
    read(quant,q.size()*2,q.data()); read(recon,rec.size()*2,rec.data());
    if (status!=1) throw std::runtime_error("Invalid-depth guard failed");
    equal(t.planes[4],q.data(),"negative quantized"); equal(t.planes[7],rec.data(),"negative reconstruction");
    std::sort(deviceTimes.begin(),deviceTimes.end()); std::sort(hostTimes.begin(),hostTimes.end());
    std::cout << "PASS trace=" << path.filename().string() << " depth=" << t.depth << " TU=" << (1u<<t.lg)
        << " blocks=" << t.blocks << " samples=" << total << " exact_stages=6 guards=1 trials=11"
        << " device_ms_median=" << deviceTimes[5] << " device_ms_max=" << deviceTimes.back()
        << " submit_read_ms_median=" << hostTimes[5] << " submit_read_ms_max=" << hostTimes.back() << '\n';
}
int main(int argc,char** argv) {
    try {
        std::cout << std::unitbuf;
        if (argc!=4) throw std::runtime_error("Usage: gpu_residual_smoke residual.cl fixture-directory vendor-substring");
        OpenCL cl;
        cl_uint n=0; check(cl.clGetPlatformIDs(0,nullptr,&n),"platform count");
        std::vector<cl_platform_id> platforms(n); check(cl.clGetPlatformIDs(n,platforms.data(),nullptr),"platforms");
        unsigned tested=0;
        for (auto p:platforms) {
            n=0; cl_int e=cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,0,nullptr,&n);
            if (e==CL_DEVICE_NOT_FOUND) continue;
            check(e,"device count"); std::vector<cl_device_id> devices(n);
            check(cl.clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,n,devices.data(),nullptr),"devices");
            for (auto id:devices) {
                if (cl.info(id,CL_DEVICE_VENDOR).find(argv[3])==std::string::npos) continue;
                std::cout << "GPU name=" << cl.info(id,CL_DEVICE_NAME) << " driver=" << cl.info(id,CL_DRIVER_VERSION) << '\n';
                std::map<std::pair<uint32_t,uint32_t>,std::unique_ptr<ResidualDevice>> variants;
                auto source=readFile(argv[1]);
                std::vector<std::filesystem::path> paths;
                for (const auto& entry:std::filesystem::recursive_directory_iterator(argv[2]))
                    if (entry.is_regular_file() && entry.path().extension()==".residual") paths.push_back(entry.path());
                std::sort(paths.begin(),paths.end());
                if (paths.empty()) throw std::runtime_error("No fixtures");
                for (const auto& path:paths) {
                    std::cout << "CASE " << path.string() << '\n';
                    ResidualTrace dimensions(path);
                    auto key=std::make_pair(dimensions.lg,dimensions.depth);
                    auto& d=variants[key];
                    if (!d) d=std::make_unique<ResidualDevice>(cl,id,source,key.first,key.second);
                    run(*d,path);
                }
                tested++;
            }
        }
        if (!tested) throw std::runtime_error("No matching GPU; no CPU fallback");
        std::cout << "RESULT PASS tested_gpus=" << tested << " scope=residual_component_only\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "RESULT FAIL " << e.what() << '\n'; return 1; }
}
