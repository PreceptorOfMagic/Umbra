// SPDX-License-Identifier: GPL-3.0-only
// GPU texture input for the development encoder. Colour functions adapted from
// Apollo adc5c5a0bd80831ce495434bb16aee2cd4175fb8:
// src_assets/windows/assets/shaders/directx/include/common.hlsl and
// convert_yuv420_packed_uv_ps_base.hlsl (LEFT_SUBSAMPLING).
// Type-0 chroma is the same [1 2 1] horizontal / [1 1] vertical footprint.
// Match the NV12/P010 path in display_vram.cpp:apply_colorspace, which uses
// color_vectors_from_colorspace (normalized legacy vectors).
// No resizing/cropping is performed; texture and encoder dimensions must agree.
inline float3 capture_srgb(float3 x) {
    x=clamp(x,0.0f,1.0f);
    return select(1.13005f*sqrt(fmax(x-0.00228f,0.0f))-0.13448f*x+0.005719f,
                  12.92f*x,x<0.0031308f);
}
inline float3 capture_pq(float3 rgb) {
    float3 r=(float3)(dot(rgb,(float3)(0.627402f,0.329292f,0.043306f)),
        dot(rgb,(float3)(0.069095f,0.919544f,0.011360f)),
        dot(rgb,(float3)(0.016394f,0.088028f,0.895578f)));
    float3 lp=pow(clamp(r*(80.0f/10000.0f),0.0f,1.0f),(float3)(2610.0f/16384.0f));
    return pow((3424.0f/4096.0f+(2413.0f/128.0f)*lp)/(1.0f+(2392.0f/128.0f)*lp),(float3)(2523.0f/32.0f));
}
inline float3 capture_curve(float3 x,uint mode) {
    return mode==2?capture_pq(x):mode==1?capture_srgb(x):clamp(x,0.0f,1.0f);
}
// Explicit normalized pixel centres and clamping pass the spatial texture
// checks on both tested drivers; unnormalized NV imports returned one colour.
inline float3 capture_sample(read_only image2d_t image,int x,int y) {
    x=clamp(x,0,get_image_width(image)-1);y=clamp(y,0,get_image_height(image)-1);
    return read_imagef(image,CLK_NORMALIZED_COORDS_TRUE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,(float2)(((float)x+0.5f)/(float)get_image_width(image),((float)y+0.5f)/(float)get_image_height(image))).xyz;
}
inline ushort capture_round(float v,uint depth) {
    // Apollo renders normalized 8-bit-range vectors to R16_UNORM for P010.
    // NVENC consumes the high ten bits: round to 16-bit UNORM, then discard six
    // low bits. Direct 4x studio-range scaling differs by up to three code values.
    if(depth==10)return (ushort)(convert_uint_sat_rte(clamp(v*64.25f,0.0f,65535.0f))>>6);
    return convert_ushort_sat(floor(clamp(v,0.0f,255.0f)+0.5f));
}
__kernel void capture_yuv(read_only image2d_t image,uint width,uint height,uint depth,uint mode,
    __global ushort* luma,__global ushort* cb,__global ushort* cr,__global uint* errors) {
    uint i=get_global_id(0);
    if(get_image_width(image)!=(int)width || get_image_height(image)!=(int)height ||
        width%2 || height%2 || (depth!=8 && depth!=10) || mode>2 || (mode==2 && depth!=10)) {
        if(!i)atomic_or(errors,1u);return;
    }
    if(i>=width*height)return;
    int x=i%width,y=i/width;
    float kr=mode==2?0.2627f:0.2126f,kb=mode==2?0.0593f:0.0722f;
    float3 weights=(float3)(kr,1.0f-kr-kb,kb);
    float scale=(float)(1u<<(depth-8));
    float3 rgb=capture_curve(capture_sample(image,x,y),mode);
    luma[i]=capture_round((16.0f+219.0f*dot(weights,rgb))*scale,depth);
    if((x&1) || (y&1))return;
    // Filter captured RGB before its transfer conversion, matching Apollo.
    rgb=(capture_sample(image,x-1,y)+2.0f*capture_sample(image,x,y)+capture_sample(image,x+1,y)+
         capture_sample(image,x-1,y+1)+2.0f*capture_sample(image,x,y+1)+capture_sample(image,x+1,y+1))*0.125f;
    rgb=capture_curve(rgb,mode);
    float value=dot(weights,rgb);
    uint chroma=(y/2)*(width/2)+x/2;
    cb[chroma]=capture_round((128.0f+112.0f*(rgb.z-value)/(1.0f-kb))*scale,depth);
    cr[chroma]=capture_round((128.0f+112.0f*(rgb.x-value)/(1.0f-kr))*scale,depth);
}
