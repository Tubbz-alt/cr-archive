/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <simd/simd.h>
#include <metal_stdlib>
#include "common.h"

using namespace metal;

struct VertexInput {
    float2 position [[attribute(VertexAttributePosition)]];
};

struct TxtVertexInput {
    float2 position [[attribute(VertexAttributePosition)]];
    float2 texCoords [[attribute(VertexAttributeTexPos)]];
};

struct ColShaderInOut {
    float4 position [[position]];
    half4  color;
};

struct StencilShaderInOut {
    float4 position [[position]];
    char color;
};

struct TxtShaderInOut {
    float4 position [[position]];
    float2 texCoords;
    float2 tpCoords;
};

struct GradShaderInOut {
    float4 position [[position]];
    float2 texCoords;
};

vertex ColShaderInOut vert_col(VertexInput in [[stage_in]],
       constant FrameUniforms& uniforms [[buffer(FrameUniformBuffer)]],
       constant TransformMatrix& transform [[buffer(MatrixBuffer)]]) {
    ColShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix*pos4;
    out.color = half4(uniforms.color.r, uniforms.color.g, uniforms.color.b, uniforms.color.a);
    return out;
}

vertex StencilShaderInOut vert_stencil(VertexInput in [[stage_in]],
       constant FrameUniforms& uniforms [[buffer(FrameUniformBuffer)]],
       constant TransformMatrix& transform [[buffer(MatrixBuffer)]]) {
    StencilShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix * pos4;
    out.color = 0xFF;
    return out;
}

vertex GradShaderInOut vert_grad(VertexInput in [[stage_in]], constant TransformMatrix& transform [[buffer(MatrixBuffer)]]) {
    GradShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix*pos4;
    return out;
}

vertex TxtShaderInOut vert_txt(TxtVertexInput in [[stage_in]], constant TransformMatrix& transform [[buffer(MatrixBuffer)]]) {
    TxtShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix*pos4;
    out.texCoords = in.texCoords;
    return out;
}

vertex TxtShaderInOut vert_txt_tp(TxtVertexInput in [[stage_in]], constant AnchorData& anchorData [[buffer(FrameUniformBuffer)]], constant TransformMatrix& transform [[buffer(MatrixBuffer)]])
{
    TxtShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix * pos4;

    // Compute texture coordinates here w.r.t. anchor rect of texture paint
    out.tpCoords.x = (anchorData.xParams[0] * in.position.x) +
                      (anchorData.xParams[1] * in.position.y) +
                      (anchorData.xParams[2] * out.position.w);
    out.tpCoords.y = (anchorData.yParams[0] * in.position.x) +
                      (anchorData.yParams[1] * in.position.y) +
                      (anchorData.yParams[2] * out.position.w);
    out.texCoords = in.texCoords;

    return out;
}

vertex GradShaderInOut vert_txt_grad(TxtVertexInput in [[stage_in]],
                                     constant TransformMatrix& transform [[buffer(MatrixBuffer)]]) {
    GradShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix*pos4;
    out.texCoords = in.texCoords;
    return out;
}

fragment half4 frag_col(ColShaderInOut in [[stage_in]]) {
    return in.color;
}

fragment unsigned int frag_stencil(StencilShaderInOut in [[stage_in]]) {
    return in.color;
}

fragment half4 frag_txt(
        TxtShaderInOut vert [[stage_in]],
        texture2d<float, access::sample> renderTexture [[texture(0)]],
        constant TxtFrameUniforms& uniforms [[buffer(1)]]
        )
{
    constexpr sampler textureSampler (mag_filter::linear,
                                  min_filter::linear);
    float4 pixelColor = renderTexture.sample(textureSampler, vert.texCoords);
    float srcA = uniforms.isSrcOpaque ? 1 : pixelColor.a;
    // TODO: consider to make shaders without IF-conditions
    if (uniforms.mode) {
        float4 c = mix(pixelColor, uniforms.color, srcA);
        return half4(c.r, c.g, c.b , c.a);
    }

    return half4(pixelColor.r,
                 pixelColor.g,
                 pixelColor.b, srcA*uniforms.extraAlpha);
}

fragment half4 frag_txt_tp(TxtShaderInOut vert [[stage_in]],
                       texture2d<float, access::sample> renderTexture [[texture(0)]],
                       texture2d<float, access::sample> paintTexture [[texture(1)]])
{
    constexpr sampler textureSampler (address::repeat,
      mag_filter::nearest,
      min_filter::nearest);

    float4 renderColor = renderTexture.sample(textureSampler, vert.texCoords);
    float4 paintColor = paintTexture.sample(textureSampler, vert.tpCoords);
    return half4(paintColor.r*renderColor.a,
                 paintColor.g*renderColor.a,
                 paintColor.b*renderColor.a,
                 renderColor.a);
}

fragment half4 frag_txt_grad(GradShaderInOut in [[stage_in]],
                         constant GradFrameUniforms& uniforms [[buffer(0)]],
                         texture2d<float, access::sample> renderTexture [[texture(0)]])
{
    constexpr sampler textureSampler (address::repeat, mag_filter::nearest,
                                      min_filter::nearest);

    float4 renderColor = renderTexture.sample(textureSampler, in.texCoords);

    float3 v = float3(in.position.x, in.position.y, 1);
    float  a = (dot(v,uniforms.params)-0.25)*2.0;
    float4 c = mix(uniforms.color1, uniforms.color2, a);
    return half4(c.r*renderColor.a,
                 c.g*renderColor.a,
                 c.b*renderColor.a,
                 renderColor.a);
}

fragment half4 aa_frag_txt(
        TxtShaderInOut vert [[stage_in]],
        texture2d<float, access::sample> renderTexture [[texture(0)]],
        constant TxtFrameUniforms& uniforms [[buffer(1)]]
)
{
    constexpr sampler textureSampler (mag_filter::linear, min_filter::linear);
    float4 pixelColor = renderTexture.sample(textureSampler, vert.texCoords);
    return half4(pixelColor.r, pixelColor.g, pixelColor.b, pixelColor.a);
}

fragment half4 frag_grad(GradShaderInOut in [[stage_in]],
                         constant GradFrameUniforms& uniforms [[buffer(0)]]) {
    float3 v = float3(in.position.x, in.position.y, 1);
    float  a = (dot(v,uniforms.params)-0.25)*2.0;
    float4 c = mix(uniforms.color1, uniforms.color2, a);
    return half4(c);
}


vertex TxtShaderInOut vert_tp(VertexInput in [[stage_in]],
       constant AnchorData& anchorData [[buffer(FrameUniformBuffer)]],
       constant TransformMatrix& transform [[buffer(MatrixBuffer)]])
{
    TxtShaderInOut out;
    float4 pos4 = float4(in.position, 0.0, 1.0);
    out.position = transform.transformMatrix * pos4;

    // Compute texture coordinates here w.r.t. anchor rect of texture paint
    out.texCoords.x = (anchorData.xParams[0] * in.position.x) +
                      (anchorData.xParams[1] * in.position.y) +
                      (anchorData.xParams[2] * out.position.w);
    out.texCoords.y = (anchorData.yParams[0] * in.position.x) +
                      (anchorData.yParams[1] * in.position.y) +
                      (anchorData.yParams[2] * out.position.w);
   
    return out;
}

fragment half4 frag_tp(
        TxtShaderInOut vert [[stage_in]],
        texture2d<float, access::sample> renderTexture [[texture(0)]])
{
    constexpr sampler textureSampler (address::repeat,
                                      mag_filter::nearest,
                                      min_filter::nearest);

    float4 pixelColor = renderTexture.sample(textureSampler, vert.texCoords);
    return half4(pixelColor.r, pixelColor.g, pixelColor.b, 1.0);

    // This implementation defaults alpha to 1.0 as if source is opaque
    //TODO : implement alpha component value if source is transparent
}

fragment half4 frag_tp_xorMode(
        TxtShaderInOut vert [[stage_in]],
        texture2d<float, access::sample> renderTexture [[texture(0)]],
        constant int& xorColor[[buffer(0)]])
{
    constexpr sampler textureSampler (address::repeat,
                                      mag_filter::nearest,
                                      min_filter::nearest);

    float4 pixelColor = renderTexture.sample(textureSampler, vert.texCoords);

    pixelColor.r = float( (unsigned char)(pixelColor.r * 255.0) ^ ((xorColor >> 16) & 0xFF) ) / 255.0f;
    pixelColor.g = float( (unsigned char)(pixelColor.g * 255.0) ^ ((xorColor >> 8) & 0xFF)) / 255.0f;
    pixelColor.b = float( (unsigned char)(pixelColor.b * 255.0) ^ (xorColor & 0xFF)) / 255.0f;
    pixelColor.a = 1.0;

    return half4(pixelColor.r, pixelColor.g, pixelColor.b, 1.0);

    // This implementation defaults alpha to 1.0 as if source is opaque
    //TODO : implement alpha component value if source is transparent
}

/* The variables involved in the equation can be expressed as follows:
 *
 *   Cs = Color component of the source (foreground color) [0.0, 1.0]
 *   Cd = Color component of the destination (background color) [0.0, 1.0]
 *   Cr = Color component to be written to the destination [0.0, 1.0]
 *   Ag = Glyph alpha (aka intensity or coverage) [0.0, 1.0]
 *   Ga = Gamma adjustment in the range [1.0, 2.5]
 *   (^ means raised to the power)
 *
 * And here is the theoretical equation approximated by this shader:
 *
 *            Cr = (Ag*(Cs^Ga) + (1-Ag)*(Cd^Ga)) ^ (1/Ga)
 */
fragment float4 lcd_color(
        TxtShaderInOut vert [[stage_in]],
        texture2d<float, access::sample> glyphTexture [[texture(0)]],
        texture2d<float, access::sample> dstTexture [[texture(1)]],
        constant LCDFrameUniforms& uniforms [[buffer(1)]]) 
{
    float3 src_adj = uniforms.src_adj;
    float3 gamma = uniforms.gamma;
    float3 invgamma = uniforms.invgamma;

    constexpr sampler glyphTextureSampler (mag_filter::linear,
                                      min_filter::linear);

    // load the RGB value from the glyph image at the current texcoord
    float3 glyph_clr = float3(glyphTexture.sample(glyphTextureSampler, vert.texCoords));

    if (glyph_clr.r == 0.0f && glyph_clr.g == 0.0f && glyph_clr.b == 0.0f) {
        // zero coverage, so skip this fragment
        discard_fragment();
    }
    constexpr sampler dstTextureSampler (mag_filter::linear,
                                      min_filter::linear);
    // load the RGB value from the corresponding destination pixel
    float3 dst_clr = float3(dstTexture.sample(dstTextureSampler, vert.texCoords));

    // gamma adjust the dest color
    float3 dst_adj = pow(dst_clr.rgb, gamma);

    // linearly interpolate the three color values
    float3 result = mix(dst_adj, src_adj, glyph_clr);

    // gamma re-adjust the resulting color (alpha is always set to 1.0)
    return float4(pow(result.rgb, invgamma), 1.0);

}
