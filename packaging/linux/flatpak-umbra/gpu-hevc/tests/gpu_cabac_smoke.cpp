// SPDX-License-Identifier: GPL-2.0-or-later
#include "cabac_test_backend.h"

static void runTrace(Device& d, const std::filesystem::path& path) {
    Trace t(path);
    auto& cl = d.cl;
    cl_uint count = static_cast<cl_uint>(t.ops.size() / 4);
    cl_uint capacity = static_cast<cl_uint>(t.expected.size() + 64), validate = 1;
    std::vector<uint8_t> output(capacity + 16, 0xa5);
    Buffer ops(d, t.ops.size() * 4, t.ops.data()), initial(d, 160, t.initial.data());
    Buffer out(d, output.size(), output.data()), finalState(d, 160), result(d, 12), saved(d, 32);
    std::unique_ptr<ParallelCabac> parallel;
    if (d.parallel) parallel = std::make_unique<ParallelCabac>(d, count, capacity);
    cl_uint activeCapacity = capacity;
    auto arg = [&](cl_uint i, size_t n, const void* p) {
        if (!d.parallel) check(cl.clSetKernelArg(d.kernel, i, n, p), "argument");
        else if (i == 4) activeCapacity = *static_cast<const cl_uint*>(p);
    };
    arg(0, sizeof(cl_mem), &ops.mem); arg(1, 4, &count); arg(2, sizeof(cl_mem), &initial.mem);
    arg(3, sizeof(cl_mem), &out.mem); arg(4, 4, &capacity); arg(5, sizeof(cl_mem), &finalState.mem);
    arg(6, sizeof(cl_mem), &result.mem); arg(7, 4, &validate);
    arg(8, sizeof(cl_mem), &saved.mem);
    auto read = [&](Buffer& b, size_t n, void* p) { check(cl.clEnqueueReadBuffer(d.queue, b.mem, CL_TRUE, 0, n, p, 0, nullptr, nullptr), "readback"); };
    auto launch = [&](cl_uint total, std::vector<cl_event>* events) {
        if (parallel) {
            parallel->launch(total, activeCapacity, validate, ops, initial, out, finalState, result, events);
            return;
        }
        size_t group = 64;
        // Bound each GPU dispatch; preserve the exact same CABAC chain on GPU.
        // This is not a slice boundary, host frame cap, or CPU encoding work.
        for (cl_uint begin = 0; begin < total || begin == 0; begin += 65536) {
            cl_uint end = std::min(total, begin + 65536);
            arg(9, 4, &begin); arg(10, 4, &end);
            cl_event event = nullptr;
            check(cl.clEnqueueNDRangeKernel(d.queue, d.kernel, 1, nullptr, &group, &group, 0, nullptr,
                                           events ? &event : nullptr), "launch");
            if (events) events->push_back(event);
        }
    };
    std::vector<double> kernelMs, wallMs;
    double maxDispatchMs = 0;
    for (int repeat = 0; repeat < 16; repeat++) {
        // First launch validates every context transition against upstream.
        validate = repeat == 0 ? 1 : 0; arg(7, 4, &validate);
        std::vector<cl_event> events;
        auto begin = std::chrono::steady_clock::now();
        launch(count, &events);
        std::array<cl_uint, 3> status = {};
        read(result, 12, status.data());
        read(out, output.size(), output.data());
        auto end = std::chrono::steady_clock::now();
        cl_ulong start = 0, finish = 0;
        auto a = cl.clGetEventProfilingInfo(events.front(), CL_PROFILING_COMMAND_START, sizeof(start), &start, nullptr);
        auto b = cl.clGetEventProfilingInfo(events.back(), CL_PROFILING_COMMAND_END, sizeof(finish), &finish, nullptr);
        std::map<std::string, double> stages;
        size_t eventIndex = 0;
        for (auto event : events) {
            cl_ulong s = 0, f = 0;
            check(cl.clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(s), &s, nullptr), "dispatch start");
            check(cl.clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(f), &f, nullptr), "dispatch end");
            if (repeat >= 5) maxDispatchMs = std::max(maxDispatchMs, double(f - s) / 1e6);
            if (parallel && repeat == 5) stages[parallel->labels[eventIndex]] += double(f - s) / 1e6;
            eventIndex++;
            cl.clReleaseEvent(event);
        }
        for (const auto& stage : stages) std::cout << "STAGE " << stage.first << " ms=" << stage.second << '\n';
        check(a, "profile start"); check(b, "profile end");
        if (status[1] || status[0] != t.expected.size())
            throw std::runtime_error(path.filename().string() + " kernel error=" + std::to_string(status[1]) +
                " op=" + std::to_string(status[2]) + " bytes=" + std::to_string(status[0]));
        if (!std::equal(t.expected.begin(), t.expected.end(), output.begin())) {
            auto at = std::mismatch(t.expected.begin(), t.expected.end(), output.begin()).first - t.expected.begin();
            throw std::runtime_error("Byte mismatch at " + std::to_string(at));
        }
        if (!std::all_of(output.begin() + t.expected.size(), output.end(), [](uint8_t x) {return x == 0xa5;}))
            throw std::runtime_error("Output guard overwritten");
        std::array<uint8_t, 160> contexts;
        read(finalState, 160, contexts.data());
        if (contexts != t.finalState) throw std::runtime_error("Final context mismatch");
        if (repeat >= 5) {
            kernelMs.push_back(double(finish - start) / 1e6);
            wallMs.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
    }
    // Capacity failure must be reported without writing past the declared bound.
    std::fill(output.begin(), output.end(), uint8_t(0xa5));
    check(cl.clEnqueueWriteBuffer(d.queue, out.mem, CL_TRUE, 0, output.size(), output.data(), 0, nullptr, nullptr), "reset guard");
    cl_uint tiny = 0; arg(4, 4, &tiny); launch(count, nullptr);
    std::array<cl_uint, 3> status = {}; read(result, 12, status.data()); read(out, output.size(), output.data());
    if (status[1] != 1 || !std::all_of(output.begin(), output.end(), [](uint8_t x) { return x == 0xa5; }))
        throw std::runtime_error("Capacity guard test failed");
    // Missing termination must not produce a purported successful stream.
    arg(4, 4, &capacity); cl_uint truncated = count - 1; arg(1, 4, &truncated); launch(truncated, nullptr);
    read(result, 12, status.data());
    if (d.parallel ? !(status[1] & 2) : status[1] != 2)
        throw std::runtime_error("Missing termination test failed");
    std::sort(kernelMs.begin(), kernelMs.end()); std::sort(wallMs.begin(), wallMs.end());
    std::cout << "PASS trace=" << path.filename().string() << " ops=" << count << " bytes=" << t.expected.size()
        << " trials=" << kernelMs.size() << " device_span_ms_median=" << kernelMs[kernelMs.size()/2]
        << " device_span_ms_max=" << kernelMs.back() << " submit_read_ms_median=" << wallMs[wallMs.size()/2]
        << " submit_read_ms_max=" << wallMs.back() << " max_dispatch_ms=" << maxDispatchMs
        << " exact_bytes=1 exact_contexts=1 guards=1\n";
}

int main(int argc, char** argv) {
    try {
        std::cout << std::unitbuf;
        if (argc != 1 && argc != 4 && !(argc == 5 && std::string(argv[4]) == "--parallel"))
            throw std::runtime_error("Usage: gpu_cabac_smoke [kernel.cl trace-directory vendor-substring [--parallel]]");
        OpenCL cl;
        cl_uint count = 0;
        check(cl.clGetPlatformIDs(0, nullptr, &count), "platform count");
        std::vector<cl_platform_id> platforms(count);
        check(cl.clGetPlatformIDs(count, platforms.data(), nullptr), "platforms");
        unsigned tested = 0, found = 0;
        for (auto platform : platforms) {
            std::cout << "PLATFORM name=" << cl.platformInfo(platform, CL_PLATFORM_NAME)
                << " version=" << cl.platformInfo(platform, CL_PLATFORM_VERSION) << '\n';
            cl_uint n = 0;
            cl_int e = cl.clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &n);
            if (e == CL_DEVICE_NOT_FOUND) continue;
            check(e, "GPU count");
            std::vector<cl_device_id> devices(n);
            check(cl.clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, n, devices.data(), nullptr), "GPUs");
            for (auto id : devices) {
                found++;
                std::string vendor = cl.info(id, CL_DEVICE_VENDOR);
                std::cout << "GPU name=" << cl.info(id, CL_DEVICE_NAME) << " vendor=" << vendor
                    << " driver=" << cl.info(id, CL_DRIVER_VERSION) << " api=" << cl.info(id, CL_DEVICE_VERSION) << '\n';
                if (argc == 1 || vendor.find(argv[3]) == std::string::npos) continue;
                Device d(cl, id, readFile(argv[1]), argc == 5);
                std::vector<std::filesystem::path> paths;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2]))
                    if (entry.is_regular_file() && entry.path().extension() == ".cabac") paths.push_back(entry.path());
                std::sort(paths.begin(), paths.end());
                if (paths.empty()) throw std::runtime_error("No traces: smoke cannot pass");
                for (const auto& path : paths) {
                    std::cout << "CASE " << path.string() << '\n';
                    runTrace(d, path);
                }
                tested++;
            }
        }
        if (!found || (argc >= 4 && !tested)) throw std::runtime_error("No matching GPU tested; no CPU fallback");
        if (argc == 1) std::cout << "INVENTORY ONLY found_gpus=" << found << " no_kernel_tested=1\n";
        else std::cout << "RESULT PASS tested_gpus=" << tested << " scope=CABAC_component_only\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RESULT FAIL " << e.what() << '\n'; return 1;
    }
}
