// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cabac_backend.h"
#include <CL/cl_d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
inline void dxCheck(HRESULT value,const char* name){if(FAILED(value))throw std::runtime_error(std::string(name)+" HRESULT="+std::to_string(value));}
struct D3D11Sharing {
    OpenCL& cl;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    cl_platform_id platform=nullptr;
    cl_device_id id=nullptr;
    clCreateFromD3D11Texture2DKHR_fn fromTexture=nullptr;
    clEnqueueAcquireD3D11ObjectsKHR_fn acquire=nullptr;
    clEnqueueReleaseD3D11ObjectsKHR_fn release=nullptr;
    std::array<cl_context_properties,5> properties={};
    bool associate() {
        auto extension=reinterpret_cast<decltype(&clGetExtensionFunctionAddressForPlatform)>(GetProcAddress(cl.module,"clGetExtensionFunctionAddressForPlatform"));
        if(!extension)throw std::runtime_error("No extension lookup");
        cl_uint n=0;check(cl.clGetPlatformIDs(0,nullptr,&n),"platform count");
        std::vector<cl_platform_id> platforms(n);check(cl.clGetPlatformIDs(n,platforms.data(),nullptr),"platforms");
            for(auto p:platforms) {
                auto extensions=cl.platformInfo(p,CL_PLATFORM_EXTENSIONS);
                const char* suffix=extensions.find("cl_khr_d3d11_sharing")!=std::string::npos?"KHR":extensions.find("cl_nv_d3d11_sharing")!=std::string::npos?"NV":nullptr;
                if(!suffix)continue;
                auto address=[&](const char* name){return extension(p,(std::string(name)+suffix).c_str());};
                auto getDevices=reinterpret_cast<clGetDeviceIDsFromD3D11KHR_fn>(address("clGetDeviceIDsFromD3D11"));
                fromTexture=reinterpret_cast<clCreateFromD3D11Texture2DKHR_fn>(address("clCreateFromD3D11Texture2D"));
                acquire=reinterpret_cast<clEnqueueAcquireD3D11ObjectsKHR_fn>(address("clEnqueueAcquireD3D11Objects"));
                release=reinterpret_cast<clEnqueueReleaseD3D11ObjectsKHR_fn>(address("clEnqueueReleaseD3D11Objects"));
                if(!getDevices || !fromTexture || !acquire || !release)continue;
                cl_uint count=0;cl_int status=getDevices(p,CL_D3D11_DEVICE_KHR,device.Get(),CL_PREFERRED_DEVICES_FOR_D3D11_KHR,0,nullptr,&count);
                if(status==CL_DEVICE_NOT_FOUND)continue;check(status,"D3D11 associated GPUs");if(!count)continue;
                std::vector<cl_device_id> ids(count);
                check(getDevices(p,CL_D3D11_DEVICE_KHR,device.Get(),CL_PREFERRED_DEVICES_FOR_D3D11_KHR,count,ids.data(),nullptr),"D3D11 associated GPU");
                platform=p;id=ids[0];
                properties={CL_CONTEXT_PLATFORM,reinterpret_cast<cl_context_properties>(p),CL_CONTEXT_D3D11_DEVICE_KHR,reinterpret_cast<cl_context_properties>(device.Get()),0};
                std::cout<<"ASSOCIATED extension="<<suffix<<" device="<<cl.info(id,CL_DEVICE_NAME)<<" driver="<<cl.info(id,CL_DRIVER_VERSION)<<'\n';return true;
            }
        return false;
    }
    D3D11Sharing(OpenCL& api,ID3D11Device* existing):cl(api),device(existing) {
        if(!existing)throw std::runtime_error("Missing D3D11 capture device");
        existing->GetImmediateContext(&context);
        ComPtr<IDXGIDevice> dxgi;dxCheck(device.As(&dxgi),"capture DXGI device");
        ComPtr<IDXGIAdapter> base;dxCheck(dxgi->GetAdapter(&base),"capture adapter");dxCheck(base.As(&adapter),"capture adapter identity");
        DXGI_ADAPTER_DESC1 desc;dxCheck(adapter->GetDesc1(&desc),"capture adapter description");
        if((desc.VendorId!=0x10de && desc.VendorId!=0x1002) || (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) || !associate())
            throw std::runtime_error("Capture device has no supported associated OpenCL GPU");
    }
    D3D11Sharing(OpenCL& api,const std::string& vendor):cl(api) {
        UINT wanted=vendor=="NVIDIA"?0x10de:vendor=="AMD"?0x1002:0;
        if(!wanted)throw std::runtime_error("Expected NVIDIA or AMD adapter");
        ComPtr<IDXGIFactory1> factory;dxCheck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        for(UINT index=0;;index++) {
            adapter.Reset();HRESULT e=factory->EnumAdapters1(index,&adapter);
            if(e==DXGI_ERROR_NOT_FOUND)break;dxCheck(e,"DXGI adapter");
            DXGI_ADAPTER_DESC1 desc;dxCheck(adapter->GetDesc1(&desc),"DXGI description");
            if(desc.VendorId!=wanted || (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            device.Reset();context.Reset();
            UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            if(GetEnvironmentVariableA("AURORA_GPU_D3D_DEBUG",nullptr,0))flags|=D3D11_CREATE_DEVICE_DEBUG;
            dxCheck(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11 device");
            if(associate())return;
        }
        throw std::runtime_error("No associated OpenCL GPU; no CPU fallback");
    }

};
struct SharedD3DImage {
    D3D11Sharing& sharing;Device& d;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> view;
    cl_mem image=nullptr;
    bool acquired=false;
    SharedD3DImage(D3D11Sharing& interop,Device& device,uint32_t w,uint32_t h,DXGI_FORMAT format):sharing(interop),d(device) {
        D3D11_TEXTURE2D_DESC desc={};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;desc.Format=format;
        desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        dxCheck(sharing.device->CreateTexture2D(&desc,nullptr,&texture),"input texture");
        dxCheck(sharing.device->CreateRenderTargetView(texture.Get(),nullptr,&view),"input view");
        cl_int e;image=sharing.fromTexture(d.context,CL_MEM_READ_ONLY,texture.Get(),0,&e);check(e,"shared input image");
    }
    void begin(){if(acquired)throw std::runtime_error("Image already acquired");check(sharing.acquire(d.queue,1,&image,0,nullptr,nullptr),"acquire input texture");acquired=true;}
    void end(){if(!acquired)throw std::runtime_error("Image not acquired");check(sharing.release(d.queue,1,&image,0,nullptr,nullptr),"release input texture");acquired=false;}
    ~SharedD3DImage(){if(acquired)sharing.release(d.queue,1,&image,0,nullptr,nullptr);d.cl.clFinish(d.queue);if(image)d.cl.clReleaseMemObject(image);}
    SharedD3DImage(const SharedD3DImage&)=delete;
};
