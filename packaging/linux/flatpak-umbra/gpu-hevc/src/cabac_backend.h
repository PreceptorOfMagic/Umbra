// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "opencl_runtime.h"

struct Trace {
    std::array<uint8_t, 160> initial, finalState;
    std::vector<uint32_t> ops;
    std::vector<uint8_t> expected;
    explicit Trace(const std::filesystem::path& path) {
        auto bytes = readFile(path);
        if (bytes.size() < 336 || memcmp(bytes.data(), "ACABAC01", 8)) throw std::runtime_error("Invalid trace magic/header");
        uint32_t count = le32(bytes.data() + 8), size = le32(bytes.data() + 12);
        if (!count || count > 16000000 || size > 32000000 ||
            bytes.size() != 336ull + 16ull * count + size) throw std::runtime_error("Invalid trace dimensions");
        std::copy_n(bytes.data() + 16, 160, initial.data());
        std::copy_n(bytes.data() + 176, 160, finalState.data());
        ops.resize(size_t(count) * 4);
        for (size_t i = 0; i < ops.size(); i++) ops[i] = le32(bytes.data() + 336 + i * 4);
        expected.assign(bytes.begin() + 336 + size_t(count) * 16, bytes.end());
    }
};

struct Device {
    OpenCL& cl;
    bool parallel;
    std::map<std::string, cl_kernel> kernels;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel kernel = nullptr;
    void close() {
        if (kernel && !parallel) cl.clReleaseKernel(kernel);
        for (const auto& entry : kernels) cl.clReleaseKernel(entry.second);
        kernels.clear();
        if (program) cl.clReleaseProgram(program);
        if (queue) cl.clReleaseCommandQueue(queue);
        if (context) cl.clReleaseContext(context);
        kernel = nullptr; program = nullptr; queue = nullptr; context = nullptr;
    }
    Device(OpenCL& api, cl_device_id id, const std::vector<uint8_t>& source, bool useParallel,
           const std::string& options = "-cl-std=CL1.2",
           const cl_context_properties* properties = nullptr) : cl(api), parallel(useParallel) {
        try {
            cl_bool endian = CL_FALSE;
            check(cl.clGetDeviceInfo(id, CL_DEVICE_ENDIAN_LITTLE, sizeof(endian), &endian, nullptr), "endian");
            if (!endian) throw std::runtime_error("Big-endian GPU not supported by trace ABI");
            cl_int error;
            context = cl.clCreateContext(properties, 1, &id, nullptr, nullptr, &error); check(error, "context");
            queue = cl.clCreateCommandQueue(context, id, CL_QUEUE_PROFILING_ENABLE, &error); check(error, "queue");
            const char* p = reinterpret_cast<const char*>(source.data());
            size_t n = source.size();
            program = cl.clCreateProgramWithSource(context, 1, &p, &n, &error); check(error, "program");
            error = cl.clBuildProgram(program, 1, &id, options.c_str(), nullptr, nullptr);
            if (error) {
                size_t size = 0;
                cl.clGetProgramBuildInfo(program, id, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
                std::vector<char> log(size + 1);
                cl.clGetProgramBuildInfo(program, id, CL_PROGRAM_BUILD_LOG, size, log.data(), nullptr);
                std::cerr << log.data() << '\n'; check(error, "kernel build");
            }
            if (parallel) {
                for (const char* name : {"cb_bucket_hist", "cb_bucket_prefix", "cb_bucket_totals", "cb_bucket_bases", "cb_bucket_scatter",
                        "cb_context_maps", "cb_compose", "cb_context_emit", "cb_range_maps", "cb_terms",
                        "cb_add_digits", "cb_carry_maps", "cb_pack", "cb_tile_maps", "cb_scan_tiles", "cb_scan_totals", "cb_scan_add"}) {
                    cl_kernel k = cl.clCreateKernel(program, name, &error); check(error, name); kernels[name] = k;
                }
                kernel = kernels.at("cb_bucket_hist");
            } else {
                kernel = cl.clCreateKernel(program, "cabac_encode", &error); check(error, "kernel");
            }
            cl_ulong localBytes = 0, privateBytes = 0;
            check(cl.clGetKernelWorkGroupInfo(kernel, id, CL_KERNEL_LOCAL_MEM_SIZE, sizeof(localBytes), &localBytes, nullptr), "local bytes");
            check(cl.clGetKernelWorkGroupInfo(kernel, id, CL_KERNEL_PRIVATE_MEM_SIZE, sizeof(privateBytes), &privateBytes, nullptr), "private bytes");
            std::cout << "KERNEL reported_local_bytes=" << localBytes << " reported_private_bytes=" << privateBytes << '\n';
            const std::string path = dumpPath();
            if (!path.empty()) {
                size_t bytes = 0;
                check(cl.clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(bytes), &bytes, nullptr), "binary size");
                std::vector<unsigned char> binary(bytes);
                unsigned char* ptr = binary.data();
                check(cl.clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(ptr), &ptr, nullptr), "binary");
                std::ofstream file(path, std::ios::binary);
                if (!file.write(reinterpret_cast<const char*>(ptr), static_cast<std::streamsize>(bytes)))
                    throw std::runtime_error("Cannot save compiled kernel");
            }
        } catch (...) { close(); throw; }
    }
    ~Device() { close(); }
};
struct Buffer {
    OpenCL& cl; cl_mem mem;
    Buffer(Device& d, size_t size, void* data = nullptr) : cl(d.cl) {
        cl_int e;
        mem = cl.clCreateBuffer(d.context, CL_MEM_READ_WRITE | (data ? CL_MEM_COPY_HOST_PTR : 0), size, data, &e);
        check(e, "buffer");
    }
    ~Buffer() { cl.clReleaseMemObject(mem); }
    Buffer(const Buffer&) = delete;
};

struct ParallelCabac {
    Device& d;
    cl_uint digitsCount;
    Buffer ranks, hist, histTotals, bases, sorted, models, contextA, contextB, rangeA, rangeB;
    Buffer contextTilesA, contextTilesB, rangeTilesA, rangeTilesB;
    Buffer terms, offsets, bitsA, bitsB, digits, carryA, carryB, error;
    Buffer bitsTilesA, bitsTilesB, carryTilesA, carryTilesB;
    std::vector<std::string> labels;
    ParallelCabac(Device& device, cl_uint count, cl_uint capacity) : d(device), digitsCount(capacity + 8),
        ranks(d, size_t(count) * 4), hist(d, size_t((count + 255) / 256) * 160 * 4),
        histTotals(d, size_t((count + 65535) / 65536) * 160 * 4), bases(d, 161 * 4),
        sorted(d, size_t(count) * 8), models(d, count),
        contextA(d, size_t((count + 31) / 32) * 128), contextB(d, size_t((count + 31) / 32) * 128),
        rangeA(d, size_t((count + 31) / 32) * 256), rangeB(d, size_t((count + 31) / 32) * 256),
        contextTilesA(d, size_t((count + 1023) / 1024) * 128), contextTilesB(d, size_t((count + 1023) / 1024) * 128),
        rangeTilesA(d, size_t((count + 1023) / 1024) * 256), rangeTilesB(d, size_t((count + 1023) / 1024) * 256),
        terms(d, size_t(count) * 8), offsets(d, size_t(count) * 4),
        bitsA(d, size_t((count + 31) / 32) * 4), bitsB(d, size_t((count + 31) / 32) * 4),
        digits(d, size_t(digitsCount) * 4), carryA(d, size_t(digitsCount) * 4), carryB(d, size_t(digitsCount) * 4), error(d, 4),
        bitsTilesA(d, size_t((count + 8191) / 8192) * 4), bitsTilesB(d, size_t((count + 8191) / 8192) * 4),
        carryTilesA(d, size_t((digitsCount + 255) / 256) * 4), carryTilesB(d, size_t((digitsCount + 255) / 256) * 4) {}

    template<class... Args>
    void call(const char* name, size_t size, size_t group, std::vector<cl_event>* events, Args... args) {
        cl_kernel k = d.kernels.at(name);
        cl_uint i = 0;
        (check(d.cl.clSetKernelArg(k, i++, sizeof(args), &args), name), ...);
        size = std::max(group, ((size + group - 1) / group) * group);
        cl_event e = nullptr;
        check(d.cl.clEnqueueNDRangeKernel(d.queue, k, 1, nullptr, &size, &group, 0, nullptr,
                                          events ? &e : nullptr), name);
        if (events) { events->push_back(e); labels.push_back(name); }
    }
    void zero(Buffer& b, size_t size, std::vector<cl_event>* events) {
        cl_uint value = 0;
        cl_event e = nullptr;
        check(d.cl.clEnqueueFillBuffer(d.queue, b.mem, &value, 4, 0, size, 0, nullptr,
                                       events ? &e : nullptr), "fill");
        if (events) { events->push_back(e); labels.push_back("clear"); }
    }
    void launch(cl_uint count, cl_uint capacity, cl_uint validate, Buffer& ops, Buffer& initial,
                Buffer& output, Buffer& finalState, Buffer& result, std::vector<cl_event>* events) {
        labels.clear();
        cl_uint blocks = std::max(1u, (count + 255) / 256), chunks = std::max(1u, (count + 31) / 32);
        cl_uint histTiles = (blocks + 255) / 256, mapTiles = (chunks + 31) / 32;
        zero(error, 4, events); zero(digits, size_t(digitsCount) * 4, events);
        call("cb_bucket_hist", size_t(blocks) * 256, 256, events, ops.mem, count, ranks.mem, hist.mem, models.mem, error.mem);
        call("cb_bucket_prefix", size_t(histTiles) * 160, 64, events, hist.mem, blocks, histTotals.mem);
        call("cb_bucket_totals", 160, 64, events, histTotals.mem, histTiles, bases.mem, initial.mem, finalState.mem);
        call("cb_bucket_bases", 1, 1, events, bases.mem);
        call("cb_bucket_scatter", count, 256, events, ops.mem, count, ranks.mem, hist.mem, histTotals.mem, bases.mem, sorted.mem);
        call("cb_context_maps", size_t(chunks) * 128, 128, events, sorted.mem, bases.mem, initial.mem, contextA.mem);
        call("cb_tile_maps", size_t(mapTiles) * 128, 128, events, contextA.mem, contextB.mem, contextTilesA.mem, chunks, cl_uint(128));
        cl_mem a = contextTilesA.mem, b = contextTilesB.mem;
        for (cl_uint step = 1; step < mapTiles; step *= 2) {
            call("cb_compose", size_t(mapTiles) * 128, 256, events, a, b, mapTiles, cl_uint(128), step);
            std::swap(a, b);
        }
        call("cb_context_emit", chunks, 64, events, sorted.mem, bases.mem, initial.mem, contextB.mem, a, models.mem,
             finalState.mem, ops.mem, validate, error.mem);
        call("cb_range_maps", size_t(chunks) * 256, 256, events, ops.mem, count, models.mem, rangeA.mem);
        call("cb_tile_maps", size_t(mapTiles) * 256, 256, events, rangeA.mem, rangeB.mem, rangeTilesA.mem, chunks, cl_uint(256));
        a = rangeTilesA.mem; b = rangeTilesB.mem;
        for (cl_uint step = 1; step < mapTiles; step *= 2) {
            call("cb_compose", size_t(mapTiles) * 256, 256, events, a, b, mapTiles, cl_uint(256), step);
            std::swap(a, b);
        }
        call("cb_terms", chunks, 64, events, ops.mem, count, models.mem, rangeB.mem, a, terms.mem, offsets.mem, bitsA.mem);
        call("cb_scan_tiles", chunks, 256, events, bitsA.mem, bitsB.mem, bitsTilesA.mem, chunks, cl_uint(0));
        call("cb_scan_totals", 256, 256, events, bitsTilesA.mem, bitsTilesB.mem, (chunks + 255) / 256, cl_uint(0));
        call("cb_scan_add", chunks, 256, events, bitsB.mem, bitsTilesB.mem, bitsA.mem, chunks, cl_uint(0));
        cl_mem bits = bitsA.mem;
        call("cb_add_digits", count, 256, events, terms.mem, offsets.mem, bits, chunks, count,
             digits.mem, digitsCount, error.mem);
        call("cb_carry_maps", digitsCount, 256, events, digits.mem, digitsCount, carryA.mem);
        call("cb_scan_tiles", digitsCount, 256, events, carryA.mem, carryB.mem, carryTilesA.mem, digitsCount, cl_uint(1));
        call("cb_scan_totals", 256, 256, events, carryTilesA.mem, carryTilesB.mem, (digitsCount + 255) / 256, cl_uint(1));
        call("cb_scan_add", digitsCount, 256, events, carryB.mem, carryTilesB.mem, carryA.mem, digitsCount, cl_uint(1));
        call("cb_pack", digitsCount, 256, events, digits.mem, carryA.mem, bits, chunks, count, output.mem, capacity,
             digitsCount, error.mem, result.mem);
    }
};
