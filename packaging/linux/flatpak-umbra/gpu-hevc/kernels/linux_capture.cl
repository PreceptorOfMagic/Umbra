// SPDX-License-Identifier: GPL-3.0-only
// Match the Windows adapter's aspect-correct linear fit, with black bars.
// Exact-size images bypass this kernel and go straight to capture_yuv.
__kernel void capture_fit(read_only image2d_t source,write_only image2d_t target,
                           uint width,uint height) {
    uint i=get_global_id(0);if(i>=width*height)return;
    int2 out=(int2)(i%width,i/width);
    float2 inSize=convert_float2((int2)(get_image_width(source),get_image_height(source)));
    float2 outSize=(float2)(width,height);
    float scale=fmin(outSize.x/inSize.x,outSize.y/inSize.y);
    float2 extent=inSize*scale,origin=(outSize-extent)*0.5f;
    float2 at=convert_float2(out)+0.5f-origin;
    float4 rgba=(float4)(0,0,0,1);
    if(all(at>=(float2)(0)) && all(at<extent))
        rgba=read_imagef(source,CLK_NORMALIZED_COORDS_TRUE|CLK_ADDRESS_CLAMP_TO_EDGE|CLK_FILTER_LINEAR,at/extent);
    write_imagef(target,out,rgba);
}
