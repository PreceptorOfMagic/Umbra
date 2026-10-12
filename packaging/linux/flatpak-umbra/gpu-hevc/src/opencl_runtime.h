#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Development component test. Never registers an Apollo encoder or a CPU fallback.
#include <CL/cl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

inline void check(cl_int e, const char* where) {
    if (e != CL_SUCCESS) throw std::runtime_error(std::string(where) + " OpenCL error " + std::to_string(e));
}
inline std::string dumpPath() {
#ifdef _WIN32
    DWORD n = GetEnvironmentVariableA("AURORA_CABAC_DUMP", nullptr, 0);
    if (!n) return {};
    std::vector<char> path(n);
    if (GetEnvironmentVariableA("AURORA_CABAC_DUMP", path.data(), n) != n - 1)
        throw std::runtime_error("Diagnostic path changed during lookup");
    return path.data();
#else
    const char* path = std::getenv("AURORA_CABAC_DUMP");
    return path ? path : "";
#endif
}
#define CL_FUNCTIONS(X) \
 X(clGetPlatformIDs) X(clGetPlatformInfo) X(clGetDeviceIDs) X(clGetDeviceInfo) \
 X(clCreateContext) X(clCreateCommandQueue) X(clCreateProgramWithSource) X(clBuildProgram) \
 X(clGetProgramBuildInfo) X(clGetProgramInfo) X(clGetKernelWorkGroupInfo) X(clCreateKernel) X(clCreateBuffer) X(clSetKernelArg) \
 X(clEnqueueNDRangeKernel) X(clEnqueueCopyBuffer) X(clEnqueueReadBuffer) X(clEnqueueWriteBuffer) X(clFinish) \
 X(clEnqueueFillBuffer) \
 X(clGetEventProfilingInfo) X(clReleaseEvent) X(clReleaseMemObject) X(clReleaseKernel) \
 X(clReleaseProgram) X(clReleaseCommandQueue) X(clReleaseContext)
struct OpenCL {
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void* module = nullptr;
#endif
#define DECLARE(name) decltype(&::name) name = nullptr;
    CL_FUNCTIONS(DECLARE)
#undef DECLARE
    OpenCL() {
#ifdef _WIN32
        module = LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#define ADDRESS(name) GetProcAddress(module, #name)
#else
        module = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
#define ADDRESS(name) dlsym(module, #name)
#endif
        if (!module) throw std::runtime_error("OpenCL runtime unavailable; no CPU fallback");
        try {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(ADDRESS(name)); if (!name) throw std::runtime_error("Missing " #name);
            CL_FUNCTIONS(LOAD)
#undef LOAD
        } catch (...) { unload(); throw; }
#undef ADDRESS
    }
    void unload() {
#ifdef _WIN32
        if (module) FreeLibrary(module);
#else
        if (module) dlclose(module);
#endif
        module = nullptr;
    }
    ~OpenCL() { unload(); }
    std::string platformInfo(cl_platform_id p, cl_platform_info key) {
        size_t size = 0;
        check(clGetPlatformInfo(p, key, 0, nullptr, &size), "platform info size");
        std::vector<char> value(size);
        check(clGetPlatformInfo(p, key, size, value.data(), nullptr), "platform info");
        return value.data();
    }
    std::string info(cl_device_id d, cl_device_info key) {
        size_t size = 0;
        check(clGetDeviceInfo(d, key, 0, nullptr, &size), "device info size");
        std::vector<char> value(size);
        check(clGetDeviceInfo(d, key, size, value.data(), nullptr), "device info");
        return value.data();
    }
};

inline std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("Cannot open " + path.string());
    auto size = f.tellg();
    if (size < 0 || size > 512 * 1024 * 1024) throw std::runtime_error("Invalid file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("Short read");
    return bytes;
}
inline uint32_t le32(const uint8_t* p) {
    return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
