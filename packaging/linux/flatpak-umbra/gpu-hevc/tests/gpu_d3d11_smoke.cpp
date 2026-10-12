// SPDX-License-Identifier: GPL-2.0-or-later
// Windows capture-integration prerequisite: D3D11 GPU texture -> OpenCL image.
// No desktop capture, display changes, encoder registration or TV interaction.
#include "opencl_test_runtime.h"
#include <CL/cl_d3d11.h>
#include <wrl/client.h>
#include <cmath>
using Microsoft::WRL::ComPtr;
static void hr(HRESULT result,const char* name){if(FAILED(result))throw std::runtime_error(std::string(name)+" HRESULT="+std::to_string(result));}
int main(int argc,char** argv){
    try{
        std::cout<<std::unitbuf;
        if(argc!=2)throw std::runtime_error("Usage: gpu_d3d11_smoke NVIDIA|AMD");
        UINT wanted=std::string(argv[1])=="NVIDIA"?0x10de:std::string(argv[1])=="AMD"?0x1002:0;
        if(!wanted)throw std::runtime_error("Unknown requested adapter vendor");
        OpenCL cl;
        auto extension=reinterpret_cast<decltype(&clGetExtensionFunctionAddressForPlatform)>(GetProcAddress(cl.module,"clGetExtensionFunctionAddressForPlatform"));
        if(!extension)throw std::runtime_error("No platform extension lookup");
        ComPtr<IDXGIFactory1> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        cl_uint n=0;check(cl.clGetPlatformIDs(0,nullptr,&n),"platform count");
        std::vector<cl_platform_id> platforms(n);check(cl.clGetPlatformIDs(n,platforms.data(),nullptr),"platforms");
        unsigned tested=0;
        for(UINT index=0;;index++){
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT found=factory->EnumAdapters1(index,&adapter);if(found==DXGI_ERROR_NOT_FOUND)break;hr(found,"adapter");
            DXGI_ADAPTER_DESC1 desc;hr(adapter->GetDesc1(&desc),"adapter description");
            if(desc.VendorId!=wanted || (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
            D3D_FEATURE_LEVEL level;
            hr(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"D3D11 device");
            for(auto platform:platforms){
                // NVIDIA's older extension uses the same signatures/token values:
                // Khronos registry, cl_nv_d3d11_sharing v2, sections 9.13.1/4/6.
                auto extensions=cl.platformInfo(platform,CL_PLATFORM_EXTENSIONS);
                const char* suffix=extensions.find("cl_khr_d3d11_sharing")!=std::string::npos?"KHR":
                    extensions.find("cl_nv_d3d11_sharing")!=std::string::npos?"NV":nullptr;
                if(!suffix)continue;
                auto address=[&](const char* name){return extension(platform,(std::string(name)+suffix).c_str());};
                auto getDevices=reinterpret_cast<clGetDeviceIDsFromD3D11KHR_fn>(address("clGetDeviceIDsFromD3D11"));
                auto fromTexture=reinterpret_cast<clCreateFromD3D11Texture2DKHR_fn>(address("clCreateFromD3D11Texture2D"));
                auto acquire=reinterpret_cast<clEnqueueAcquireD3D11ObjectsKHR_fn>(address("clEnqueueAcquireD3D11Objects"));
                auto release=reinterpret_cast<clEnqueueReleaseD3D11ObjectsKHR_fn>(address("clEnqueueReleaseD3D11Objects"));
                if(!getDevices || !fromTexture || !acquire || !release)continue;
                cl_uint count=0;cl_int e=getDevices(platform,CL_D3D11_DEVICE_KHR,device.Get(),CL_PREFERRED_DEVICES_FOR_D3D11_KHR,0,nullptr,&count);
                if(e==CL_DEVICE_NOT_FOUND || !count)continue;
                check(e,"D3D11-associated device count");std::vector<cl_device_id> ids(count);
                check(getDevices(platform,CL_D3D11_DEVICE_KHR,device.Get(),CL_PREFERRED_DEVICES_FOR_D3D11_KHR,count,ids.data(),nullptr),"D3D11-associated devices");
                for(auto id:ids){
                    std::cout<<"ASSOCIATED extension="<<suffix<<" platform="<<cl.platformInfo(platform,CL_PLATFORM_NAME)<<" device="<<cl.info(id,CL_DEVICE_NAME)<<" driver="<<cl.info(id,CL_DRIVER_VERSION)<<'\n';
                    cl_context_properties properties[]={CL_CONTEXT_PLATFORM,reinterpret_cast<cl_context_properties>(platform),CL_CONTEXT_D3D11_DEVICE_KHR,reinterpret_cast<cl_context_properties>(device.Get()),0};
                    cl_context compute=cl.clCreateContext(properties,1,&id,nullptr,nullptr,&e);check(e,"sharing context");
                    cl_command_queue queue=cl.clCreateCommandQueue(compute,id,0,&e);check(e,"queue");
                    const char* source="__kernel void sample_image(read_only image2d_t image,__global float4* pixels){int x=get_global_id(0);pixels[x]=read_imagef(image,CLK_NORMALIZED_COORDS_FALSE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,(int2)(x%16,x/16));}";
                    size_t length=strlen(source);cl_program program=cl.clCreateProgramWithSource(compute,1,&source,&length,&e);check(e,"program");
                    check(cl.clBuildProgram(program,1,&id,"-cl-std=CL1.2",nullptr,nullptr),"image reader build");
                    cl_kernel kernel=cl.clCreateKernel(program,"sample_image",&e);check(e,"kernel");
                    cl_mem output=cl.clCreateBuffer(compute,CL_MEM_READ_WRITE,16*16*4*sizeof(float),nullptr,&e);check(e,"output");
                    for(DXGI_FORMAT format:{DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT}){
                        D3D11_TEXTURE2D_DESC textureDesc={};textureDesc.Width=16;textureDesc.Height=16;textureDesc.MipLevels=1;textureDesc.ArraySize=1;
                        textureDesc.Format=format;textureDesc.SampleDesc.Count=1;textureDesc.Usage=D3D11_USAGE_DEFAULT;
                        textureDesc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;textureDesc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
                        ComPtr<ID3D11Texture2D> texture;hr(device->CreateTexture2D(&textureDesc,nullptr,&texture),"texture");
                        ComPtr<ID3D11RenderTargetView> view;hr(device->CreateRenderTargetView(texture.Get(),nullptr,&view),"render target");
                        cl_mem image=fromTexture(compute,CL_MEM_READ_ONLY,texture.Get(),0,&e);check(e,"shared image");
                        check(cl.clSetKernelArg(kernel,0,sizeof(image),&image),"image arg");check(cl.clSetKernelArg(kernel,1,sizeof(output),&output),"output arg");
                        for(unsigned repeat=0;repeat<3;repeat++){
                            const float color[4]={repeat==1?1.5f:0.25f,repeat==2?0.125f:0.5f,0.75f,1.0f};
                            context->ClearRenderTargetView(view.Get(),color);context->Flush();
                            check(acquire(queue,1,&image,0,nullptr,nullptr),"acquire D3D11 image");size_t global=256;
                            check(cl.clEnqueueNDRangeKernel(queue,kernel,1,nullptr,&global,nullptr,0,nullptr,nullptr),"read image on GPU");
                            check(release(queue,1,&image,0,nullptr,nullptr),"release D3D11 image");
                            std::vector<float> pixels(256*4);check(cl.clEnqueueReadBuffer(queue,output,CL_TRUE,0,pixels.size()*sizeof(float),pixels.data(),0,nullptr,nullptr),"verification readback");
                            for(size_t i=0;i<pixels.size();i++){
                                float expected=format==DXGI_FORMAT_B8G8R8A8_UNORM?std::min(color[i%4],1.0f):color[i%4];
                                if(!std::isfinite(pixels[i]) || std::abs(pixels[i]-expected)> (format==DXGI_FORMAT_B8G8R8A8_UNORM?0.004f:0.0001f))throw std::runtime_error("Shared texture pixels differ");
                            }
                        }
                        cl.clReleaseMemObject(image);std::cout<<"PASS format="<<unsigned(format)<<" frames=3 pixels=256 scRGB_above_one="<<(format==DXGI_FORMAT_R16G16B16A16_FLOAT)<<'\n';
                    }
                    cl.clReleaseMemObject(output);cl.clReleaseKernel(kernel);cl.clReleaseProgram(program);cl.clReleaseCommandQueue(queue);cl.clReleaseContext(compute);tested++;
                }
            }
        }
        if(!tested)throw std::runtime_error("No D3D11-associated OpenCL GPU available");
        std::cout<<"RESULT PASS associated_devices="<<tested<<" scope=GPU_texture_sharing_not_desktop_capture_or_HDR_presentation\n";return 0;
    }catch(const std::exception& e){std::cerr<<"RESULT FAIL "<<e.what()<<'\n';return 1;}
}
