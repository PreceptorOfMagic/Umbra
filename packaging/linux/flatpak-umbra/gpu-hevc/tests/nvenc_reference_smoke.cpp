// SPDX-License-Identifier: GPL-3.0-only
// Reference component: unchanged Apollo native NVENC implementation and shaders.
// Fresh offscreen Pattern only; no display/session/network/service operations.
#include "d3d11_pattern.h"
#include "src/nvenc/nvenc_d3d11_native.h"
#include "src/nvenc/nvenc_utils.h"
#include "src/video_colorspace.h"
#include <bcrypt.h>
#include <iomanip>
#include <set>
#include <sstream>

namespace reference {
struct Manifest {
    std::map<std::string,std::string> entries;
    explicit Manifest(const char* path) {
        std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open reference manifest");
        std::string line;
        while(std::getline(input,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();
            if(line.empty()||line[0]=='#')continue;
            auto at=line.find('=');
            if(at==std::string::npos||!at||!entries.emplace(line.substr(0,at),line.substr(at+1)).second)
                throw std::runtime_error("Malformed or duplicate manifest line: "+line);
        }
    }
    std::string take(const std::string& key) {
        auto it=entries.find(key);if(it==entries.end()||it->second.empty())throw std::runtime_error("Missing explicit manifest field: "+key);
        auto value=it->second;entries.erase(it);std::cout<<"CONFIG "<<key<<'='<<value<<'\n';return value;
    }
    int number(const std::string& key) {
        auto value=take(key);size_t used=0;int result=std::stoi(value,&used);
        if(used!=value.size())throw std::runtime_error("Not an integer: "+key);return result;
    }
    bool boolean(const std::string& key) {
        auto value=take(key);if(value=="true")return true;if(value=="false")return false;
        throw std::runtime_error("Expected true or false: "+key);
    }
    void exhausted()const {if(!entries.empty())throw std::runtime_error("Unknown manifest field: "+entries.begin()->first);}
};

struct Configuration {
    nvenc::nvenc_config nv;
    video::config_t client{};
    unsigned frames=0,patternFlags=0;
    std::set<unsigned> forceIdr;
    explicit Configuration(Manifest& m) {
        // Evidence identifiers are mandatory. Their factual provenance is audited
        // by the conductor, not inferred from a filename or this parser.
        m.take("evidence.owner_source");m.take("evidence.host_config");m.take("evidence.session_config");
#define READ_NV_INT(field) nv.field=m.number("nv." #field)
#define READ_NV_BOOL(field) nv.field=m.boolean("nv." #field)
        READ_NV_INT(quality_preset);READ_NV_INT(gop_length);READ_NV_BOOL(slice_rows);READ_NV_INT(constrained);
        auto pass=m.take("nv.two_pass");
        if(pass=="disabled")nv.two_pass=nvenc::nvenc_two_pass::disabled;
        else if(pass=="quarter_res")nv.two_pass=nvenc::nvenc_two_pass::quarter_resolution;
        else if(pass=="full_res")nv.two_pass=nvenc::nvenc_two_pass::full_resolution;
        else throw std::runtime_error("Unknown explicit NVENC two-pass value");
        READ_NV_INT(vbv_percentage_increase);READ_NV_BOOL(weighted_prediction);READ_NV_BOOL(adaptive_quantization);
        READ_NV_BOOL(enable_min_qp);READ_NV_INT(min_qp_h264);READ_NV_INT(min_qp_hevc);READ_NV_INT(min_qp_av1);
        READ_NV_BOOL(h264_cavlc);READ_NV_BOOL(h264_main_profile);READ_NV_BOOL(insert_filler_data);READ_NV_BOOL(intra_refresh);
#undef READ_NV_INT
#undef READ_NV_BOOL
#define READ_CLIENT(field) client.field=m.number("client." #field)
        READ_CLIENT(width);READ_CLIENT(height);READ_CLIENT(framerate);READ_CLIENT(bitrate);READ_CLIENT(slicesPerFrame);
        READ_CLIENT(numRefFrames);READ_CLIENT(encoderCscMode);READ_CLIENT(videoFormat);READ_CLIENT(dynamicRange);
        READ_CLIENT(chromaSamplingType);READ_CLIENT(enableIntraRefresh);READ_CLIENT(encodingFramerate);
#undef READ_CLIENT
        client.input_only=m.boolean("client.input_only");client.aurora_gpu_hevc=m.boolean("client.aurora_gpu_hevc");
        int count=m.number("test.frames"),flags=m.number("test.pattern_flags");
        if(count<=0||flags<0||(flags&~572))throw std::runtime_error("Unsupported component sequence");
        frames=unsigned(count);patternFlags=unsigned(flags);
        auto idrs=m.take("test.force_idr_frames");
        if(idrs!="none") {
            std::istringstream list(idrs);std::string value;
            while(std::getline(list,value,',')) {size_t used=0;auto f=std::stoul(value,&used);
                if(used!=value.size()||f>=frames||!forceIdr.insert(unsigned(f)).second)throw std::runtime_error("Invalid explicit IDR schedule");}
        }
        m.exhausted();
        if(client.width<=0||client.height<=0||client.width%2||client.height%2||client.framerate<=0||client.bitrate<=0)
            throw std::runtime_error("Invalid explicit reference geometry/rate");
        if(client.videoFormat!=1||(client.dynamicRange!=0&&client.dynamicRange!=1)||client.chromaSamplingType!=0||client.input_only||client.aurora_gpu_hevc)
            throw std::runtime_error("Reference supports native HEVC 4:2:0 SDR8 or FP16-source HDR10 only");
        // No quality/preset/GOP/rate retuning and no automatic setting fallback.
    }
};

ComPtr<ID3DBlob> ownerShader(const std::filesystem::path& path,const char* entry,const char* model) {
    ComPtr<ID3DBlob> blob,messages;
    auto status=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,model,
                                   D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&messages);
    if(messages)std::cerr.write(static_cast<const char*>(messages->GetBufferPointer()),std::streamsize(messages->GetBufferSize()));
    dxCheck(status,"unchanged Apollo shader");return blob;
}

template<class T> ComPtr<ID3D11Buffer> constantBuffer(ID3D11Device* device,const T& value) {
    static_assert(sizeof(T)%16==0);D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(T);desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=&value;
    ComPtr<ID3D11Buffer> result;dxCheck(device->CreateBuffer(&desc,&data,&result),"owner shader constant buffer");return result;
}

struct OwnerConversion {
    ComPtr<ID3D11VertexShader> yVs,uvVs;ComPtr<ID3D11PixelShader> yPs,uvPs;
    ComPtr<ID3D11Buffer> matrix,offset,rotation;ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RenderTargetView> yView,uvView;
    unsigned width,height;
    OwnerConversion(ID3D11Device* device,ID3D11Texture2D* destination,const std::filesystem::path& shaders,
                    const video::sunshine_colorspace_t& colors,unsigned w,unsigned h,bool hdr):width(w),height(h) {
        auto makeVs=[&](const char* name,ComPtr<ID3D11VertexShader>& out) {auto code=ownerShader(shaders/name,"main_vs","vs_5_0");
            dxCheck(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&out),"owner vertex shader");};
        auto makePs=[&](const char* name,ComPtr<ID3D11PixelShader>& out) {auto code=ownerShader(shaders/name,"main_ps","ps_5_0");
            dxCheck(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&out),"owner pixel shader");};
        // Same-size source: select Apollo display_vram.cpp's FP16/P010 HDR
        // perceptual-quantizer shaders, retaining its SDR shader pair verbatim.
        makeVs("convert_yuv420_planar_y_vs.hlsl",yVs);
        makePs(hdr?"convert_yuv420_planar_y_ps_perceptual_quantizer.hlsl":"convert_yuv420_planar_y_ps.hlsl",yPs);
        makeVs("convert_yuv420_packed_uv_type0_vs.hlsl",uvVs);
        makePs(hdr?"convert_yuv420_packed_uv_type0_ps_perceptual_quantizer.hlsl":"convert_yuv420_packed_uv_type0_ps.hlsl",uvPs);
        auto vectors=video::color_vectors_from_colorspace(colors);if(!vectors)throw std::runtime_error("Owner color vectors unavailable");
        matrix=constantBuffer(device,*vectors);offset=constantBuffer(device,std::array<float,4>{1.f/w,1.f/h,0,0});
        rotation=constantBuffer(device,std::array<int32_t,4>{0,0,0,0});
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        sd.MinLOD=0;sd.MaxLOD=D3D11_FLOAT32_MAX;dxCheck(device->CreateSamplerState(&sd,&sampler),"owner linear sampler");
        D3D11_RENDER_TARGET_VIEW_DESC vd{};vd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        vd.Format=hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM;
        dxCheck(device->CreateRenderTargetView(destination,&vd,&yView),"owner Y target");
        vd.Format=hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM;
        dxCheck(device->CreateRenderTargetView(destination,&vd,&uvView),"owner UV target");
    }
    void draw(ID3D11DeviceContext* context,ID3D11ShaderResourceView* source) {
        auto cb=matrix.Get();context->PSSetConstantBuffers(0,1,&cb);cb=offset.Get();context->VSSetConstantBuffers(0,1,&cb);
        cb=rotation.Get();context->VSSetConstantBuffers(1,1,&cb);auto ss=sampler.Get();context->PSSetSamplers(0,1,&ss);
        context->PSSetShaderResources(0,1,&source);context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};auto target=yView.Get();context->OMSetRenderTargets(1,&target,nullptr);
        context->RSSetViewports(1,&viewport);context->VSSetShader(yVs.Get(),nullptr,0);context->PSSetShader(yPs.Get(),nullptr,0);context->Draw(3,0);
        target=uvView.Get();viewport.Width/=2;viewport.Height/=2;context->OMSetRenderTargets(1,&target,nullptr);context->RSSetViewports(1,&viewport);
        context->VSSetShader(uvVs.Get(),nullptr,0);context->PSSetShader(uvPs.Get(),nullptr,0);context->Draw(3,0);
        ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(0,1,&none);context->OMSetRenderTargets(0,nullptr,nullptr);context->Flush();
    }
};

std::string digest(const std::vector<uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    auto checkStatus=[](NTSTATUS status){if(status<0)throw std::runtime_error("BCrypt SHA256 failed");};
    checkStatus(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    try {
        checkStatus(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0));
        checkStatus(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0));
        std::array<uint8_t,32> result{};checkStatus(BCryptFinishHash(hash,result.data(),ULONG(result.size()),0));
        BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
        std::ostringstream text;for(auto value:result)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(value);return text.str();
    }catch(...){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);throw;}
}

ComPtr<ID3D11Texture2D> staging(ID3D11Device* device,ID3D11Texture2D* original) {
    D3D11_TEXTURE2D_DESC desc{};original->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> out;dxCheck(device->CreateTexture2D(&desc,nullptr,&out),"observer staging texture");return out;
}

std::vector<uint8_t> readback(ID3D11DeviceContext* context,ID3D11Texture2D* original,ID3D11Texture2D* copy,unsigned rowBytes,unsigned rows) {
    context->CopyResource(copy,original);D3D11_MAPPED_SUBRESOURCE mapped{};dxCheck(context->Map(copy,0,D3D11_MAP_READ,0,&mapped),"observer map");
    std::vector<uint8_t> result(size_t(rowBytes)*rows);
    for(unsigned y=0;y<rows;++y)std::memcpy(result.data()+size_t(y)*rowBytes,static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,rowBytes);
    context->Unmap(copy,0);return result;
}

void write(std::ofstream& output,const std::vector<uint8_t>& bytes) {
    if(!output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Observer artifact write failed");
}
}

int main(int argc,char** argv) {try {
    if(argc!=4 && !(argc==5 && std::string(argv[4])=="--audit-input"))throw std::runtime_error("Usage: nvenc_reference_smoke explicit-reference.conf owner-shader-directory output-prefix [--audit-input]");
    const bool auditInput=argc==5;
    std::cout<<std::unitbuf;reference::Manifest manifest(argv[1]);reference::Configuration config(manifest);
    const unsigned width=unsigned(config.client.width),height=unsigned(config.client.height);
    const bool hdr=config.client.dynamicRange==1;
    // All configuration validation occurs before any hardware encoder is opened.
    // Select the same physical D3D11/OpenCL-associated GPU as the beta component.
    OpenCL cl;D3D11Sharing gpu(cl,"NVIDIA");Pattern pattern(gpu.device.Get());
    auto colors=video::colorspace_from_client_config(config.client,hdr);
    std::unique_ptr<nvenc::nvenc_d3d11_native> encoder;ComPtr<ID3D11Texture2D> auditTexture;ID3D11Texture2D* input=nullptr;
    if(auditInput) {
        D3D11_TEXTURE2D_DESC auditDesc{};auditDesc.Width=width;auditDesc.Height=height;auditDesc.MipLevels=1;auditDesc.ArraySize=1;
        auditDesc.Format=hdr?DXGI_FORMAT_P010:DXGI_FORMAT_NV12;auditDesc.SampleDesc.Count=1;auditDesc.Usage=D3D11_USAGE_DEFAULT;auditDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        dxCheck(gpu.device->CreateTexture2D(&auditDesc,nullptr,&auditTexture),"shader input audit texture");input=auditTexture.Get();
    }else {
        encoder=std::make_unique<nvenc::nvenc_d3d11_native>(gpu.device.Get());
        if(!encoder->create_encoder(config.nv,config.client,nvenc::nvenc_colorspace_from_sunshine_colorspace(colors),hdr?NV_ENC_BUFFER_FORMAT_YUV420_10BIT:NV_ENC_BUFFER_FORMAT_NV12))
            throw std::runtime_error("Original Apollo NVENC initialization failed");
        input=encoder->get_input_texture();
    }
    reference::OwnerConversion conversion(gpu.device.Get(),input,argv[2],colors,width,height,hdr);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> rgb;ComPtr<ID3D11RenderTargetView> rgbTarget;ComPtr<ID3D11ShaderResourceView> rgbSource;
    dxCheck(gpu.device->CreateTexture2D(&desc,nullptr,&rgb),"fresh Pattern texture");dxCheck(gpu.device->CreateRenderTargetView(rgb.Get(),nullptr,&rgbTarget),"Pattern target");
    dxCheck(gpu.device->CreateShaderResourceView(rgb.Get(),nullptr,&rgbSource),"Pattern shader resource");
    auto rgbCopy=reference::staging(gpu.device.Get(),rgb.Get()),inputCopy=reference::staging(gpu.device.Get(),input);
    std::string prefix=argv[3];
    for(auto suffix:{".hevc",".input.yuv",".source-hashes.txt"})if(std::filesystem::exists(prefix+suffix))throw std::runtime_error("Refusing to overwrite reference artifacts");
    std::ofstream stream,source(prefix+".input.yuv",std::ios::binary),hashes(prefix+".source-hashes.txt");
    if(!auditInput)stream.open(prefix+".hevc",std::ios::binary);
    if((!auditInput&&!stream)||!source||!hashes)throw std::runtime_error("Cannot open reference output artifacts");
    uint64_t bytes=0;
    for(unsigned frame=auditInput?config.frames-1:0;frame<config.frames;++frame) {
        auto start=std::chrono::steady_clock::now();pattern.draw(rgbTarget.Get(),config.patternFlags&8?0:frame,(config.patternFlags&548)|(hdr?2u:0u),width,height);
        conversion.draw(gpu.context.Get(),rgbSource.Get());auto encodeStart=std::chrono::steady_clock::now();
        if(!auditInput) {
            auto encoded=encoder->encode_frame(frame,config.forceIdr.count(frame)!=0);auto end=std::chrono::steady_clock::now();
            if(encoded.data.empty()||encoded.frame_index!=frame)throw std::runtime_error("Original NVENC produced missing/misidentified frame");
            reference::write(stream,encoded.data);bytes+=encoded.data.size();
            std::cout<<"REFERENCE_FRAME frame="<<frame<<" bytes="<<encoded.data.size()<<" idr="<<encoded.idr
                     <<" submit_ms="<<std::chrono::duration<double,std::milli>(end-start).count()
                     <<" encode_ms="<<std::chrono::duration<double,std::milli>(end-encodeStart).count()<<'\n';
        }
        // Readbacks observe fresh GPU input outside both timed intervals; no CPU
        // pixels enter NVENC and no reconstruction is substituted for the input.
        auto rgbBytes=reference::readback(gpu.context.Get(),rgb.Get(),rgbCopy.Get(),width*(hdr?8u:4u),height);
        auto native=reference::readback(gpu.context.Get(),input,inputCopy.Get(),width*(hdr?2u:1u),height+height/2);
        const size_t lumaSamples=size_t(width)*height,chromaSamples=lumaSamples/4;
        std::vector<uint8_t> planar(lumaSamples*3/2*(hdr?2:1));
        if(hdr) {
            // P010 stores each sample in the high ten bits of a little-endian
            // word. The observer emits low-aligned planar yuv420p10le for FFmpeg.
            auto unpack=[&](size_t sourceSample,size_t targetSample) {
                unsigned value=(unsigned(native[sourceSample*2])|(unsigned(native[sourceSample*2+1])<<8))>>6;
                planar[targetSample*2]=uint8_t(value);planar[targetSample*2+1]=uint8_t(value>>8);
            };
            for(size_t i=0;i<lumaSamples;++i)unpack(i,i);
            for(size_t i=0;i<chromaSamples;++i){unpack(lumaSamples+i*2,lumaSamples+i);unpack(lumaSamples+i*2+1,lumaSamples+chromaSamples+i);}
        }else {
            std::copy_n(native.begin(),lumaSamples,planar.begin());
            for(size_t i=0;i<chromaSamples;++i){planar[lumaSamples+i]=native[lumaSamples+i*2];planar[lumaSamples+chromaSamples+i]=native[lumaSamples+i*2+1];}
        }
        reference::write(source,planar);
        hashes<<"SOURCE frame="<<frame<<(hdr?" fp16_sha256=":" bgra_sha256=")<<reference::digest(rgbBytes)<<" input_yuv_sha256="<<reference::digest(planar)<<'\n';
        if(!hashes)throw std::runtime_error("Source hash write failed");
    }
    if(auditInput){std::cout<<"SOURCE_AUDIT COMPLETE frames=1 source_frame="<<config.frames-1<<" hardware_encoder_opened=0\n";return 0;}
    std::cout<<"REFERENCE COMPLETE frames="<<config.frames<<" bytes="<<bytes
             <<" bitrate_at_requested_rate_kbps="<<double(bytes)*8*config.client.framerate/config.frames/1000
             <<" independent_decode_quality_required=1\n";
    return 0;
}catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}}
