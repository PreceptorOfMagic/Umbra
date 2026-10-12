// SPDX-License-Identifier: GPL-3.0-only
// Compile the actual Apollo GPU capture adapter against minimal type/log seams.
// This tests D3D shared-handle/mutex/lifetime behavior, not Apollo networking.
#include "picture_test_io.h"
#include "d3d11_pattern.h"
#include "aurora_gpu_hevc.h"
#include <d3d11_1.h>
#include <unordered_map>
#define BOOST_LOG(level) std::cout
namespace util { static long log_hex(HRESULT hr){return hr;} }
namespace video {
enum class colorspace_e {rec709,bt2020};
struct sunshine_colorspace_t {colorspace_e colorspace;bool full_range;unsigned bit_depth;};
struct config_t {int width=0,height=0,videoFormat=1,chromaSamplingType=0,dynamicRange=0,bitrate=0,framerate=0;};
static sunshine_colorspace_t colorspace_from_client_config(const config_t& c,bool hdr){return {hdr?colorspace_e::bt2020:colorspace_e::rec709,false,c.dynamicRange?10u:8u};}
static bool colorspace_is_hdr(const sunshine_colorspace_t& c){return c.colorspace==colorspace_e::bt2020;}
}
namespace platf {
struct img_t:std::enable_shared_from_this<img_t> {int width=0,height=0;virtual ~img_t()=default;};
struct gpu_hevc_encode_device_t {
 video::sunshine_colorspace_t colorspace{};
 virtual ~gpu_hevc_encode_device_t()=default;
 virtual int convert(img_t&)=0;
 virtual bool encode(uint64_t,bool,std::vector<uint8_t>&,bool&)=0;
};
namespace dxgi {
template<class T> struct Handle {
 using pointer=T*;ComPtr<T> value;
 T* get()const{return value.Get();}T* operator->()const{return get();}
 explicit operator bool()const{return get()!=nullptr;}
 T** operator&(){return value.GetAddressOf();}
 void reset(){value.Reset();}
};
using adapter_t=Handle<IDXGIAdapter1>;using device_t=Handle<ID3D11Device>;
using device1_t=Handle<ID3D11Device1>;using device_ctx_t=Handle<ID3D11DeviceContext>;
using texture2d_t=Handle<ID3D11Texture2D>;using render_target_t=Handle<ID3D11RenderTargetView>;using keyed_mutex_t=Handle<IDXGIKeyedMutex>;
struct img_d3d_t:img_t {
 texture2d_t capture_texture;render_target_t capture_rt;keyed_mutex_t capture_mutex;
 HANDLE encoder_texture_handle=nullptr;bool dummy=false,blank=true;uint32_t id=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 ~img_d3d_t(){if(encoder_texture_handle)CloseHandle(encoder_texture_handle);}
};
#include "gpu_hevc_device.inc"
}}
static void makeImage(platf::dxgi::img_d3d_t& img,D3D11Sharing& capture,unsigned width,unsigned height,unsigned mode) {
 img.capture_texture.reset();img.capture_rt.reset();img.capture_mutex.reset();
 if(img.encoder_texture_handle){CloseHandle(img.encoder_texture_handle);img.encoder_texture_handle=nullptr;}
 img.width=int(width);img.height=int(height);img.dummy=false;img.blank=false;
 img.format=mode?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
 D3D11_TEXTURE2D_DESC desc={};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=1;
 desc.Format=img.format;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
 dxCheck(capture.device->CreateTexture2D(&desc,nullptr,&img.capture_texture),"capture texture");
 dxCheck(capture.device->CreateRenderTargetView(img.capture_texture.get(),nullptr,&img.capture_rt),"capture view");
 dxCheck(img.capture_texture->QueryInterface(IID_PPV_ARGS(&img.capture_mutex)),"capture mutex");
 ComPtr<IDXGIResource1> resource;dxCheck(img.capture_texture->QueryInterface(IID_PPV_ARGS(&resource)),"capture resource");
 dxCheck(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ,nullptr,&img.encoder_texture_handle),"capture handle");
}
int main(int argc,char** argv){try {
 if(argc!=6)throw std::runtime_error("Usage: apollo_adapter_smoke headers output-prefix NVIDIA|AMD mode expected-library.hevc");
 std::cout<<std::unitbuf;PictureHeaders h(argv[1]);unsigned mode=unsigned(std::stoul(argv[4]));OpenCL cl;D3D11Sharing capture(cl,argv[3]);Pattern pattern(capture.device.Get());
 // Fixed-QP fixture isolates capture/mutex correctness from wall-clock-driven
 // rate control. Production rate/AQ pictures have separate decode/live checks.
 video::config_t config;config.width=int(h.width);config.height=int(h.height);config.dynamicRange=h.depth==10;config.bitrate=0;config.framerate=0;
 platf::dxgi::d3d_gpu_hevc_encode_device_t encoder;
 if(!encoder.init(capture.adapter.Get(),config,mode==2))throw std::runtime_error("Adapter init failed");
 auto img=std::make_shared<platf::dxgi::img_d3d_t>();img->width=config.width;img->height=config.height;img->dummy=true;
 if(encoder.convert(*img))throw std::runtime_error("Startup dummy rejected");
 // Actually encode the defined startup black, then force the first real capture IDR.
 std::vector<uint8_t> bytes;bool idr=false;
 if(!encoder.encode(1,false,bytes,idr)||!idr||bytes.empty())throw std::runtime_error("Startup dummy encode failed");
 {std::ofstream f(std::string(argv[2])+".dummy.hevc",std::ios::binary);append(f,bytes);}
 std::ofstream stream(std::string(argv[2])+".hevc",std::ios::binary);
 for(uint32_t frame=0;frame<h.frames;frame++){
  // Same image ID receives a new texture midway; stale mappings must be discarded.
  if(frame==0||frame==7)makeImage(*img,capture,h.width,h.height,mode);
  dxCheck(img->capture_mutex->AcquireSync(0,1000),"producer acquire");
  pattern.draw(img->capture_rt.get(),frame,mode,h.width,h.height);
  dxCheck(img->capture_mutex->ReleaseSync(0),"producer release");
  if(encoder.convert(*img))throw std::runtime_error("Capture adapter convert failed");
  // Recycle the capture texture before encode. The adapter's private GPU copy
  // must still contain this frame, not the overwritten producer pixels.
  dxCheck(img->capture_mutex->AcquireSync(0,1000),"producer recycle acquire");
  const float black[]={0,0,0,1};capture.context->ClearRenderTargetView(img->capture_rt.get(),black);capture.context->Flush();
  dxCheck(img->capture_mutex->ReleaseSync(0),"producer recycle release");
  const bool wantIdr=h.types[frame]==20;
  if(!encoder.encode(uint64_t(frame)*3+10,wantIdr,bytes,idr)||idr!=wantIdr)throw std::runtime_error("Adapter encode failed");
  append(stream,bytes);
 }
 stream.close();auto actual=readFile(std::filesystem::path(std::string(argv[2])+".hevc"));auto expected=readFile(argv[5]);
 if(actual!=expected)throw std::runtime_error("Adapter capture pixels differ from verified library stream");
 img->width--;if(!encoder.convert(*img))throw std::runtime_error("Capture mismatch accepted");
 std::cout<<"ADAPTER PASS capture_and_encode_devices_distinct=1 shared_NT_handle=1 keyed_mutex=1 producer_recycle=1 image_identity_reuse=1 exact_bytes=1 frames="<<h.frames<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
