// SPDX-License-Identifier: GPL-3.0-only
#include "picture_test_io.h"
#include "d3d11_pattern.h"
#include "aurora_gpu_hevc.h"
struct Library {
    HMODULE module=nullptr;
    decltype(&agh_open) open=nullptr;decltype(&agh_encode) encode=nullptr;decltype(&agh_close) close=nullptr;
    explicit Library(const char* path) {
        auto absolute=std::filesystem::absolute(path);
        module=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("GPU encoder library load failed Windows="+std::to_string(GetLastError()));
        open=reinterpret_cast<decltype(open)>(GetProcAddress(module,"agh_open"));encode=reinterpret_cast<decltype(encode)>(GetProcAddress(module,"agh_encode"));close=reinterpret_cast<decltype(close)>(GetProcAddress(module,"agh_close"));
        if(!open || !encode || !close){FreeLibrary(module);module=nullptr;throw std::runtime_error("GPU encoder ABI exports missing");}
    }
    ~Library(){if(module)FreeLibrary(module);}
};
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        if(argc!=7 && argc!=8)throw std::runtime_error("Usage: gpu_library_smoke library kernels header-fixture output-prefix NVIDIA|AMD mode [rate]");
        const bool rate=argc==8 && std::string(argv[7])=="rate";
        if(argc==8 && !rate)throw std::runtime_error("Unknown library smoke mode");
        PictureHeaders h(argv[3]);unsigned mode=unsigned(std::stoul(argv[6]));if(mode>2 || (mode==2 && h.depth!=10))throw std::runtime_error("Invalid mode");
        OpenCL cl;D3D11Sharing sharing(cl,argv[5]);Pattern pattern(sharing.device.Get());Library lib(argv[1]);
        D3D11_TEXTURE2D_DESC desc={};desc.Width=h.width;desc.Height=h.height;desc.MipLevels=1;desc.ArraySize=1;desc.SampleDesc.Count=1;
        desc.Format=mode?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> view;
        dxCheck(sharing.device->CreateTexture2D(&desc,nullptr,&texture),"pattern input");dxCheck(sharing.device->CreateRenderTargetView(texture.Get(),nullptr,&view),"pattern view");
        agh_config config={sizeof(agh_config),AGH_ABI_VERSION,h.width,h.height,h.depth,mode==2?1u:0u,sharing.device.Get(),argv[2]};
        if(rate){config.bitrate_kbps=50000;config.framerate=165;}
        char error[1024]={};auto invalid=config;invalid.abi_version++;
        if(auto bad=lib.open(&invalid,error,sizeof(error))){lib.close(bad);throw std::runtime_error("Invalid ABI accepted");}
        if(!*error)throw std::runtime_error("Invalid ABI missing error");
        for(unsigned connection=0;connection<2;connection++) {
            std::unique_ptr<agh_session,decltype(lib.close)> session(lib.open(&config,error,sizeof(error)),lib.close);
            if(!session)throw std::runtime_error(error);
            std::string prefix=std::string(argv[4])+"-connection"+std::to_string(connection);
            std::ofstream actual(prefix+".hevc",std::ios::binary),canonical(prefix+".canonical.hevc",std::ios::binary);
            for(uint32_t frame=0;frame<h.frames;frame++) {
                bool intra=h.types[frame]==20;pattern.draw(view.Get(),frame,mode,h.width,h.height);
                agh_packet packet={};packet.size=sizeof(packet);
                if(lib.encode(session.get(),texture.Get(),uint64_t(frame)*3+10,intra?1u:0u,&packet,error,sizeof(error)))throw std::runtime_error(error);
                if(!packet.data || !packet.bytes || packet.is_idr!=(intra?1u:0u) || packet.frame_id!=uint64_t(frame)*3+10)throw std::runtime_error("GPU packet identity mismatch");
                if(!actual.write(reinterpret_cast<const char*>(packet.data),std::streamsize(packet.bytes)))throw std::runtime_error("Packet write failed");
                size_t skip=!rate && intra && frame?h.parameterSets.size():0;
                if(skip && (packet.bytes<skip || memcmp(packet.data,h.parameterSets.data(),skip)))throw std::runtime_error("Repeated parameter sets differ");
                if(!canonical.write(reinterpret_cast<const char*>(packet.data+skip),std::streamsize(packet.bytes-skip)))throw std::runtime_error("Canonical write failed");
                std::cout<<"LIBRARY_FRAME connection="<<connection<<" frame="<<frame<<" id="<<packet.frame_id<<" idr="<<packet.is_idr<<" bytes="<<packet.bytes<<" submit_packet_ms="<<packet.submit_to_packet_ms<<'\n';
            }
            agh_packet packet={};packet.size=sizeof(packet);
            if(!lib.encode(session.get(),nullptr,100000,0,&packet,error,sizeof(error)) || !*error || packet.bytes)throw std::runtime_error("Missing capture was accepted");
            if(!lib.encode(session.get(),texture.Get(),100001,0,&packet,error,sizeof(error)) || !*error || packet.bytes)throw std::runtime_error("Failed session resumed silently");
            std::cout<<"CONNECTION PASS index="<<connection<<" missing_capture_rejected=1 failed_session_reuse_rejected=1 independent_decode_check_required=1\n";
        }
        std::cout<<"RESULT PASS connections=2 no_TV_or_Apollo_deployment=1\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
