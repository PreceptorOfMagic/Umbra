// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "d3d11_test_backend.h"
#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
static ComPtr<ID3DBlob> shader(const char* source,const char* entry,const char* profile) {
    ComPtr<ID3DBlob> bytes,errors;
    HRESULT e=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytes,&errors);
    if(FAILED(e) && errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
    dxCheck(e,"test-pattern shader");return bytes;
}
struct Pattern {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;ComPtr<ID3D11DepthStencilState> depthStencil;
    // Only populated when AURORA_PATTERN_SOURCE names a raw BGRA8 frame; otherwise every
    // path below is exactly as it was and the recorded fixtures reproduce byte-for-byte.
    ComPtr<ID3D11PixelShader> psSource;ComPtr<ID3D11ShaderResourceView> sourceView;
    ComPtr<ID3D11SamplerState> sourceSampler;bool haveSource=false;
    std::vector<ComPtr<ID3D11ShaderResourceView>> sequenceViews;ComPtr<ID3D11PixelShader> psSourceSeq;
    Pattern(ID3D11Device* deviceSource):device(deviceSource) {
        device->GetImmediateContext(&context);
        const char* source=R"(
cbuffer configuration:register(b0){uint frame;uint mode;uint width;uint height;};
float4 vs_main(uint id:SV_VertexID):SV_Position{return id==0?float4(-1,-1,0,1):id==1?float4(-1,3,0,1):float4(3,-1,0,1);}
float noise_at(float2 p) {
    uint2 b=(uint2)((int2)p+int2(65536,65536));
    uint a=b.x^(b.y*747796405u);a=((a>>16)^a)*2246822519u;a=((a>>13)^a)*3266489917u;a^=a>>16;
    return (a&65535)/65535.0;
}
float texture_at(float2 p) {
    float2 cell=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(noise_at(cell),noise_at(cell+float2(1,0)),f.x),lerp(noise_at(cell+float2(0,1)),noise_at(cell+1),f.x),f.y);
}
float4 ps_main(float4 at:SV_Position):SV_Target {
    uint x=(uint)at.x,y=(uint)at.y;
    float3 rgb=float3((x+frame*7)&255,(y+frame*3)&255,((x/13)^(y/11))&255)/255.0;
    if((mode&4) && y<height/3) {
        uint a=(x+frame*3)^(y*747796405u)^((frame/30)*2891336453u);
        a=((a>>16)^a)*2246822519u;a=((a>>13)^a)*3266489917u;a^=a>>16;
        rgb=float3(a&255,(a>>8)&255,(a>>16)&255)/255.0;
    }
    if(mode&32) {
        // A continuously translated textured surface, not independently changing
        // random noise. Fractional movement exercises prediction efficiency.
        float2 p=at.xy-float2(0.75,0.25)*frame;
        float broad=0.18*sin(p.x*0.021)+0.14*cos(p.y*0.033);
        float detail=0.12*sin(p.x*0.73)*cos(p.y*0.49)+0.08*sin((p.x+p.y)*0.19);
        rgb=saturate(float3(0.50+broad+detail,0.48+broad*0.8+detail,0.46+broad*0.6+detail));
    }
    if(mode&512) {
        // Untuned holdout scene: continuous panning detail at several scales,
        // an independently moving object, sharp tile edges, and fixed HUD marks.
        float2 p=at.xy+float2(2.25,-1.25)*frame;
        float t=0.48*texture_at(p/13)+0.26*texture_at(p/3)+0.16*texture_at(p/1.1);
        rgb=float3(0.08+0.52*t,0.13+0.65*t,0.05+0.40*t);
        if(frac(p.x/149)<0.015 || frac(p.y/97)<0.02)rgb*=0.35;
        float2 obj=at.xy-float2(width*0.5+sin(frame*0.047)*width*0.22,height*0.5+cos(frame*0.061)*height*0.2);
        if(dot(obj,obj)<22500)rgb=float3(0.7+0.15*sin(obj.x*0.08),0.18+0.1*cos(obj.y*0.09),0.08);
        if(y<72 && x<720) {
            rgb*=0.15;
            uint2 q=uint2(x/3,y/3);uint glyph=(q.x/6)%10, gx=q.x%6, gy=q.y%8;
            uint bits=(glyph*2654435761u)^((gy+1)*2246822519u);
            if(gx<5 && gy<7 && ((bits>>gx)&1))rgb=float3(0.9,0.93,0.95);
        }
        if(abs((int)x-(int)(width/2))<1 || abs((int)y-(int)(height/2))<1)rgb=float3(1,1,1);
    }
    if((mode&3)==2)rgb*=8.0;
    return float4(rgb,1.0);
}
// REAL-FRAME SOURCE. Samples a captured game frame instead of synthesising content, panned
// by the same fractional offset the holdout fixture uses so prediction is exercised the way
// real motion exercises it. GetDimensions keeps the constant buffer unchanged, so the
// synthetic path above stays bit-exact.
Texture2D<float4> source_image:register(t0);
SamplerState source_sampler:register(s0);
float4 ps_source(float4 at:SV_Position):SV_Target {
    uint sw,sh;source_image.GetDimensions(sw,sh);
    float2 p=at.xy+float2(2.25,-1.25)*frame;
    float3 rgb=source_image.Sample(source_sampler,p/float2(sw,sh)).rgb;
    if((mode&3)==2)rgb*=8.0;
    return float4(rgb,1.0);
}
// SEQUENCE: a real captured burst. No pan -- the motion is whatever the game actually did
// between those frames, which is the entire point of capturing a burst rather than panning
// one still. The frame texture is selected on the CPU side and bound before the draw.
float4 ps_source_seq(float4 at:SV_Position):SV_Target {
    uint sw,sh;source_image.GetDimensions(sw,sh);
    float3 rgb=source_image.Sample(source_sampler,at.xy/float2(sw,sh)).rgb;
    if((mode&3)==2)rgb*=8.0;
    return float4(rgb,1.0);
})";
        auto v=shader(source,"vs_main","vs_5_0"),p=shader(source,"ps_main","ps_5_0");
        dxCheck(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs),"pattern VS");
        dxCheck(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps),"pattern PS");
        D3D11_BUFFER_DESC desc={};desc.ByteWidth=16;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.Usage=D3D11_USAGE_DEFAULT;
        dxCheck(device->CreateBuffer(&desc,nullptr,&constants),"pattern constants");
        D3D11_RASTERIZER_DESC raster={};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        dxCheck(device->CreateRasterizerState(&raster,&rasterizer),"pattern rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth={};depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;
        dxCheck(device->CreateDepthStencilState(&depth,&depthStencil),"pattern depth state");
        // _dupenv_s / stoul rather than getenv / sscanf: this project builds the smokes with
        // /W4 /WX and MSVC treats the classic forms as deprecation errors.
        auto envValue=[](const char* name)->std::string {
            char* raw=nullptr;size_t length=0;
            if(_dupenv_s(&raw,&length,name)!=0 || !raw)return std::string();
            std::string value(raw);free(raw);return value;
        };
        const std::string sourcePath=envValue("AURORA_PATTERN_SOURCE");
        const std::string sourceSize=envValue("AURORA_PATTERN_SOURCE_SIZE");
        if(!sourcePath.empty() && !sourceSize.empty()) {
            const size_t split=sourceSize.find('x');
            if(split==std::string::npos)throw std::runtime_error("AURORA_PATTERN_SOURCE_SIZE must be WxH");
            const uint32_t sw=(uint32_t)std::stoul(sourceSize.substr(0,split));
            const uint32_t sh=(uint32_t)std::stoul(sourceSize.substr(split+1));
            if(!sw || !sh)throw std::runtime_error("AURORA_PATTERN_SOURCE_SIZE must be WxH");
            std::ifstream in(sourcePath,std::ios::binary);
            if(!in)throw std::runtime_error("cannot open AURORA_PATTERN_SOURCE "+sourcePath);
            std::vector<uint8_t> pixels((size_t)sw*sh*4);
            in.read(reinterpret_cast<char*>(pixels.data()),(std::streamsize)pixels.size());
            if((size_t)in.gcount()!=pixels.size())
                throw std::runtime_error("AURORA_PATTERN_SOURCE is not width*height*4 bytes of BGRA");
            D3D11_TEXTURE2D_DESC tex={};tex.Width=sw;tex.Height=sh;tex.MipLevels=1;tex.ArraySize=1;
            tex.Format=DXGI_FORMAT_B8G8R8A8_UNORM;tex.SampleDesc.Count=1;tex.Usage=D3D11_USAGE_IMMUTABLE;
            tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init={};init.pSysMem=pixels.data();init.SysMemPitch=sw*4;
            ComPtr<ID3D11Texture2D> texture;
            dxCheck(device->CreateTexture2D(&tex,&init,&texture),"pattern source texture");
            dxCheck(device->CreateShaderResourceView(texture.Get(),nullptr,&sourceView),"pattern source view");
            D3D11_SAMPLER_DESC samp={};samp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samp.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;samp.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;
            samp.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;samp.MaxLOD=D3D11_FLOAT32_MAX;
            dxCheck(device->CreateSamplerState(&samp,&sourceSampler),"pattern source sampler");
            auto sp=shader(source,"ps_source","ps_5_0");
            dxCheck(device->CreatePixelShader(sp->GetBufferPointer(),sp->GetBufferSize(),nullptr,&psSource),"pattern source PS");
            haveSource=true;
            std::cerr<<"PATTERN_SOURCE "<<sourcePath<<" "<<sw<<"x"<<sh<<std::endl;
            // A burst: <path-without-suffix>-000.bgra .. -NNN.bgra, one texture each.
            const std::string countText=envValue("AURORA_PATTERN_SOURCE_COUNT");
            if(!countText.empty()) {
                const uint32_t frames=(uint32_t)std::stoul(countText);
                const size_t dash=sourcePath.rfind("-000.bgra");
                if(dash==std::string::npos)throw std::runtime_error("AURORA_PATTERN_SOURCE_COUNT needs a -000.bgra source");
                const std::string stem=sourcePath.substr(0,dash);
                for(uint32_t i=0;i<frames;++i) {
                    char suffix[32];snprintf(suffix,sizeof(suffix),"-%03u.bgra",i);
                    const std::string each=stem+suffix;
                    std::ifstream fs(each,std::ios::binary);
                    if(!fs)throw std::runtime_error("cannot open sequence frame "+each);
                    std::vector<uint8_t> bytes((size_t)sw*sh*4);
                    fs.read(reinterpret_cast<char*>(bytes.data()),(std::streamsize)bytes.size());
                    if((size_t)fs.gcount()!=bytes.size())throw std::runtime_error("short sequence frame "+each);
                    D3D11_TEXTURE2D_DESC td={};td.Width=sw;td.Height=sh;td.MipLevels=1;td.ArraySize=1;
                    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;
                    td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                    D3D11_SUBRESOURCE_DATA sub={};sub.pSysMem=bytes.data();sub.SysMemPitch=sw*4;
                    ComPtr<ID3D11Texture2D> frameTexture;ComPtr<ID3D11ShaderResourceView> frameView;
                    dxCheck(device->CreateTexture2D(&td,&sub,&frameTexture),"sequence texture");
                    dxCheck(device->CreateShaderResourceView(frameTexture.Get(),nullptr,&frameView),"sequence view");
                    sequenceViews.push_back(frameView);
                }
                auto sq=shader(source,"ps_source_seq","ps_5_0");
                dxCheck(device->CreatePixelShader(sq->GetBufferPointer(),sq->GetBufferSize(),nullptr,&psSourceSeq),"sequence PS");
                std::cerr<<"PATTERN_SEQUENCE "<<stem<<" frames="<<sequenceViews.size()<<std::endl;
            }
        }
    }
    void draw(ID3D11RenderTargetView* view,uint32_t frame,uint32_t mode,uint32_t width,uint32_t height) {
        auto c=context.Get();uint32_t values[4]={frame,mode,width,height};c->UpdateSubresource(constants.Get(),0,nullptr,values,0,0);
        auto cb=constants.Get();c->PSSetConstantBuffers(0,1,&cb);c->VSSetShader(vs.Get(),nullptr,0);
        const bool sequence=!sequenceViews.empty();
        c->PSSetShader(sequence?psSourceSeq.Get():(haveSource?psSource.Get():ps.Get()),nullptr,0);
        if(haveSource){
            auto sv=sequence?sequenceViews[frame%sequenceViews.size()].Get():sourceView.Get();
            auto ss=sourceSampler.Get();c->PSSetShaderResources(0,1,&sv);c->PSSetSamplers(0,1,&ss);
        }
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->RSSetState(rasterizer.Get());c->OMSetDepthStencilState(depthStencil.Get(),0);
        c->OMSetBlendState(nullptr,nullptr,0xffffffffu);c->GSSetShader(nullptr,nullptr,0);c->HSSetShader(nullptr,nullptr,0);c->DSSetShader(nullptr,nullptr,0);
        D3D11_VIEWPORT viewport={0,0,float(width),float(height),0,1};c->RSSetViewports(1,&viewport);
        c->OMSetRenderTargets(1,&view,nullptr);c->Draw(3,0);c->OMSetRenderTargets(0,nullptr,nullptr);c->Flush();

    }
};
