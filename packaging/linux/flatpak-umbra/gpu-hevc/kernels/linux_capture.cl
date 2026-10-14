// SPDX-License-Identifier: GPL-3.0-only
// Match the Windows adapter's aspect-correct linear fit, with black bars.
// Exact-size images bypass this kernel and go straight to capture_yuv.
__kernel void capture_fit(read_only image2d_t source,write_only image2d_t target,
                           uint width,uint height,uint visibleWidth,uint visibleHeight) {
    uint i=get_global_id(0);if(i>=width*height)return;
    int2 out=(int2)(i%width,i/width);
    float2 inSize=convert_float2((int2)(get_image_width(source),get_image_height(source)));
    float2 outSize=(float2)(visibleWidth,visibleHeight);
    float scale=fmin(outSize.x/inSize.x,outSize.y/inSize.y);
    float2 extent=inSize*scale,origin=(outSize-extent)*0.5f;
    float2 at=convert_float2(out)+0.5f-origin;
    float4 rgba=(float4)(0,0,0,1);
    if(all(at>=(float2)(0)) && all(at<extent))
        rgba=read_imagef(source,CLK_NORMALIZED_COORDS_TRUE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_LINEAR,at/extent);
    write_imagef(target,out,rgba);
}

// Explicit PipeWire xBGR210LE input, BT.2020/ST2084. Read byte lanes so the
// packed contract is independent of OpenCL's host/device integer endianness.
// Inverse of capture.cl's 709-to-2020 matrix; preserve extended/negative scRGB.
// Fitting therefore happens in linear light before the shared Windows PQ path.
static float4 pq_word_to_scrgb(uint value) {
    float3 pq=convert_float3((uint3)(value&1023,(value>>10)&1023,(value>>20)&1023))/1023.0f;
    float3 p=pow(pq,(float3)(32.0f/2523.0f));
    float3 rgb=125.0f*pow(fmax(p-3424.0f/4096.0f,0.0f)/(2413.0f/128.0f-(2392.0f/128.0f)*p),
        (float3)(16384.0f/2610.0f));
    float3 scrgb=(float3)(dot(rgb,(float3)(1.660490f,-0.587638f,-0.072853f)),
        dot(rgb,(float3)(-0.124550f,1.132898f,-0.008347f)),
        dot(rgb,(float3)(-0.018151f,-0.100579f,1.118730f)));
    return (float4)(scrgb,1.0f);
}
__kernel void capture_unpack_pq(read_only image2d_t packed,write_only image2d_t linear,
                                uint width,uint height) {
    uint i=get_global_id(0);if(i>=width*height)return;
    int2 at=(int2)(i%width,i/width);
    uint4 bytes=read_imageui(packed,CLK_NORMALIZED_COORDS_TRUE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,
        (convert_float2(at)+0.5f)/(float2)(width,height));
    write_imagef(linear,at,pq_word_to_scrgb(bytes.x|(bytes.y<<8)|(bytes.z<<16)|(bytes.w<<24)));
}

// GL-shared frames (AGHL_FRAME_GL_TEXTURE): the caller's GL_R32UI texture, one word per pixel holding the
// format's byte layout, is read here directly into the input image, so the frame stays on the GPU. Mesa
// rusticl copies images to/from buffers on the CPU where the driver lacks buffer/texture copies (radeonsi,
// "clEnqueueCopyImageToBuffer stalls the GPU"). bgra: 1 = bytes B,G,R,A (AGHL_BGRA8), 0 = R,G,B,A.
__kernel void capture_unpack_word(read_only image2d_t words,write_only image2d_t target,
                                  uint width,uint height,uint bgra) {
    uint i=get_global_id(0);if(i>=width*height)return;
    int2 at=(int2)(i%width,i/width);
    uint value=read_imageui(words,CLK_NORMALIZED_COORDS_FALSE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,at).x;
    float4 bytes=convert_float4((uint4)(value&255u,(value>>8)&255u,(value>>16)&255u,value>>24))/255.0f;
    write_imagef(target,at,bgra?bytes.zyxw:bytes);
}
__kernel void capture_unpack_pq_word(read_only image2d_t words,write_only image2d_t linear,
                                     uint width,uint height) {
    uint i=get_global_id(0);if(i>=width*height)return;
    int2 at=(int2)(i%width,i/width);
    write_imagef(linear,at,pq_word_to_scrgb(
        read_imageui(words,CLK_NORMALIZED_COORDS_FALSE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_NEAREST,at).x));
}
