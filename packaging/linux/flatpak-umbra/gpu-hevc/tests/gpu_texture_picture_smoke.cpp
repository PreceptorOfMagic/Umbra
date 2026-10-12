// SPDX-License-Identifier: GPL-3.0-only
// D3D11-generated moving input -> shared OpenCL texture -> GPU HEVC pictures.
// Readbacks of input/reconstruction are test observers, never encoder inputs.
#include "picture_test_io.h"
#include "d3d11_test_backend.h"
#include "d3d11_pattern.h"
#include <cmath>
static std::array<double,3> curve(std::array<double,3> rgb,unsigned mode) {
    if(mode==2) {
        rgb={0.627402*rgb[0]+0.329292*rgb[1]+0.043306*rgb[2],
             0.069095*rgb[0]+0.919544*rgb[1]+0.011360*rgb[2],
             0.016394*rgb[0]+0.088028*rgb[1]+0.895578*rgb[2]};
        for(double& v:rgb){double p=std::pow(std::clamp(v*80/10000,0.0,1.0),2610.0/16384);v=std::pow((3424.0/4096+2413.0/128*p)/(1+2392.0/128*p),2523.0/32);}
    } else for(double& v:rgb){v=std::clamp(v,0.0,1.0);if(mode==1)v=v<0.0031308?12.92*v:1.13005*std::sqrt(v-0.00228)-0.13448*v+0.005719;}
    return rgb;
}
static void verify(GpuPictureEncoder& encoder,Buffer& source,unsigned mode,unsigned frame) {
    const auto& h=encoder.h;std::vector<float> rgb(size_t(h.width)*h.height*4);encoder.read(source,rgb.size()*4,rgb.data());
    // Independent observation that the texture contains the newly rendered frame.
    for(uint32_t y=0;y<h.height;y+=17)for(uint32_t x=0;x<h.width;x+=19){
        double expected[3]={double((x+frame*7)&255)/255,double((y+frame*3)&255)/255,double(((x/13)^(y/11))&255)/255};
        for(unsigned c=0;c<3;c++){
            if(mode==2)expected[c]*=8;
            double got=rgb[(size_t(y)*h.width+x)*4+c];
            if(!std::isfinite(got) || std::abs(got-expected[c])>0.004)throw std::runtime_error("Stale/wrong D3D11 pattern pixels x="+std::to_string(x)+" y="+std::to_string(y)+" channel="+std::to_string(c)+" expected="+std::to_string(expected[c])+" actual="+std::to_string(got));
        }
    }
    auto sample=[&](int x,int y,unsigned c){return double(rgb[(size_t(std::clamp(y,0,int(h.height)-1))*h.width+std::clamp(x,0,int(h.width)-1))*4+c]);};
    double kr=mode==2?0.2627:0.2126,kb=mode==2?0.0593:0.0722,scale=double(1u<<(h.depth-8));
    unsigned maximum=0;
    for(unsigned c=0;c<3;c++) {
        auto& plane=*encoder.planes[c];std::vector<uint16_t> pixels(size_t(plane.width)*plane.height);encoder.read(plane.input,pixels.size()*2,pixels.data());
        for(uint32_t y=0;y<plane.height;y++)for(uint32_t x=0;x<plane.width;x++){
            std::array<double,3> color={};
            for(unsigned channel=0;channel<3;channel++){
                if(!c)color[channel]=sample(x,y,channel);
                else for(int dy=0;dy<2;dy++)for(int dx=-1;dx<=1;dx++)color[channel]+=sample(int(x)*2+dx,int(y)*2+dy,channel)*(dx==0?2:1)/8.0;
            }
            color=curve(color,mode);double value=kr*color[0]+(1-kr-kb)*color[1]+kb*color[2];
            double out=c==0?16+219*value:c==1?128+112*(color[2]-value)/(1-kb):128+112*(color[0]-value)/(1-kr);
            int expected=int(std::floor(out*scale+0.5)),actual=pixels[size_t(y)*plane.width+x];
            unsigned difference=unsigned(std::abs(expected-actual));maximum=std::max(maximum,difference);
            if(difference>1)throw std::runtime_error("GPU colour conversion differs from double-precision reference plane="+std::to_string(c)+" x="+std::to_string(x)+" y="+std::to_string(y)+" expected="+std::to_string(expected)+" actual="+std::to_string(actual));
        }
    }
    std::cout<<"COLOR PASS frame="<<frame<<" mode="<<mode<<" max_lsb_difference="<<maximum<<'\n';
}
int main(int argc,char** argv) {
    try {
        std::cout<<std::unitbuf;
        if(argc!=6)throw std::runtime_error("Usage: gpu_texture_picture_smoke kernels headers output-prefix NVIDIA|AMD mode(0=SDR8texture,1=scRGB-SDR,2=scRGB-HDR)");
        PictureHeaders h(argv[2]);unsigned mode=unsigned(std::stoul(argv[5]));if(mode>2 || (mode==2 && h.depth!=10))throw std::runtime_error("Invalid texture mode");
        std::filesystem::path dir(argv[1]);OpenCL cl;D3D11Sharing sharing(cl,argv[4]);
        Device d(cl,sharing.id,pictureProgram(dir),true,"-cl-std=CL1.2",sharing.properties.data());
        initPictureKernels(d,sharing.id,dir,h.depth);
        auto conversion=readFile(dir/"capture.cl");
        const char* observer=R"(
__kernel void observe_source(read_only image2d_t image,__global float4* out){uint i=get_global_id(0);uint w=get_image_width(image);if(i<w*get_image_height(image))out[i]=read_imagef(image,CLK_NORMALIZED_COORDS_TRUE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,(float2)(((float)(i%w)+0.5f)/(float)w,((float)(i/w)+0.5f)/(float)get_image_height(image)));}
)";
        conversion.insert(conversion.end(),observer,observer+strlen(observer));
        addProgram(d,sharing.id,conversion,"-cl-std=CL1.2",{{"capture_yuv","capture_yuv"},{"observe_source","observe_source"}});
        GpuPictureEncoder encoder(d,h);ParallelCabac dispatch(d,1,1);Buffer error(d,4),source(d,size_t(h.width)*h.height*16);
        SharedD3DImage image(sharing,d,h.width,h.height,mode?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_B8G8R8A8_UNORM);Pattern pattern(sharing.device.Get());
        std::ofstream stream(std::string(argv[3])+".hevc",std::ios::binary),recon(std::string(argv[3])+".recon.yuv",std::ios::binary),input(std::string(argv[3])+".input.yuv",std::ios::binary);
        append(stream,h.parameterSets);
        for(uint32_t frame=0;frame<h.frames;frame++) {
            auto begin=std::chrono::steady_clock::now();pattern.draw(image.view.Get(),frame,mode,h.width,h.height);image.begin();dispatch.zero(error,4,nullptr);
            dispatch.call("capture_yuv",size_t(h.width)*h.height,256,nullptr,image.image,h.width,h.height,h.depth,cl_uint(mode),
                encoder.planes[0]->input.mem,encoder.planes[1]->input.mem,encoder.planes[2]->input.mem,error.mem);
            image.end();auto encoded=encoder.encode(h.types[frame]==20,h.contexts[frame].data());
            auto end=std::chrono::steady_clock::now();uint32_t errors;encoder.read(error,4,&errors);if(errors)throw std::runtime_error("Capture conversion rejected dimensions/mode");
            std::cout<<"TEXTURE_FRAME "<<frame<<" type="<<(h.types[frame]==20?'I':'P')<<" render_acquire_convert_encode_read_ms="<<std::chrono::duration<double,std::milli>(end-begin).count()<<" encode_submit_read_ms="<<encoded.submitReadMs<<'\n';
            writeNal(stream,h.types[frame],h.slices[frame],encoded.bytes);
            image.begin();dispatch.call("observe_source",size_t(h.width)*h.height,256,nullptr,image.image,source.mem);image.end();verify(encoder,source,mode,frame);
            if(h.types[frame]!=20)encoder.auditMotion(frame);
            for(unsigned c=0;c<3;c++) {
                auto pixels=encoder.reconstruction(c);std::vector<uint16_t> inputPixels(pixels.size());encoder.read(encoder.planes[c]->input,pixels.size()*2,inputPixels.data());
                if(h.depth==8){
                    std::vector<uint8_t> packed(pixels.size()),inputPacked(inputPixels.size());
                    std::transform(pixels.begin(),pixels.end(),packed.begin(),[](uint16_t v){return uint8_t(v);});
                    std::transform(inputPixels.begin(),inputPixels.end(),inputPacked.begin(),[](uint16_t v){return uint8_t(v);});
                    append(recon,packed);append(input,inputPacked);
                }
                else {
                    if(!recon.write(reinterpret_cast<char*>(pixels.data()),std::streamsize(pixels.size()*2)) || !input.write(reinterpret_cast<char*>(inputPixels.data()),std::streamsize(inputPixels.size()*2)))throw std::runtime_error("Test observer write failed");
                }
            }
        }
        std::cout<<"ENCODE COMPLETE frames="<<h.frames<<" scope=GPU_rendered_texture_not_desktop_or_TV independent_decode_check_required=1\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
