#include "MetalShaderSources.h"

const char* const kMetalMeshShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float scalar [[attribute(3)]];
};

struct Uniforms {
    float4x4 mvp;
    float4 color;
    float4 contour;
};

struct VertexOut {
    float4 position [[position]];
    float3 normal;
    float3 color;
    float scalar;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.normal = normalize(in.normal);
    out.color = in.color;
    out.scalar = in.scalar;
    return out;
}

float3 jetColor(float t)
{
    t = clamp(t, 0.0, 1.0);
    float r = 0.0;
    float g = 0.0;
    float b = 0.0;
    if (t < 0.125) {
        b = 0.5 + t / 0.125 * 0.5;
    } else if (t < 0.375) {
        g = (t - 0.125) / 0.25;
        b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) / 0.25;
        g = 1.0;
        b = 1.0 - (t - 0.375) / 0.25;
    } else if (t < 0.875) {
        r = 1.0;
        g = 1.0 - (t - 0.625) / 0.25;
    } else {
        r = 1.0 - (t - 0.875) / 0.125 * 0.5;
    }
    return float3(r, g, b);
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]])
{
    float3 lightDir = normalize(float3(-0.4, -0.7, -0.5));
    float diffuse = max(dot(normalize(in.normal), -lightDir), 0.0);
    float shade = 0.28 + 0.72 * diffuse;
    float3 baseColor = in.color;
    if (uniforms.contour.w > 0.5) {
        float range = uniforms.contour.y - uniforms.contour.x;
        float t = range > 1.0e-10
            ? clamp((in.scalar - uniforms.contour.x) / range, 0.0, 1.0)
            : 0.5;
        int numBands = max(1, int(uniforms.contour.z + 0.5));
        int band = int(t * float(numBands));
        if (band >= numBands) {
            band = numBands - 1;
        }
        float qt = (float(band) + 0.5) / float(numBands);
        baseColor = jetColor(qt);
    }
    return float4(baseColor * shade, uniforms.color.a);
}
)";

const char* const kMetalBackgroundShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 color;
    float4 gridParams;
};

struct BackgroundUniforms {
    float4 bottomColor;
    float4 topColor;
    float4 gridParams;
};

vertex VertexOut vertex_main(uint vertexId [[vertex_id]],
                             constant BackgroundUniforms& uniforms [[buffer(0)]])
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };
    float2 pos = positions[vertexId];
    float t = clamp(pos.y * 0.5 + 0.5, 0.0, 1.0);
    VertexOut out;
    out.position = float4(pos, 0.0, 1.0);
    out.color = mix(uniforms.bottomColor.xyz, uniforms.topColor.xyz, t);
    out.gridParams = uniforms.gridParams;
    return out;
}

float3 applyViewportGrid(float3 baseColor, float2 fragCoord, float4 gridParams)
{
    float minorStep = max(gridParams.y, 1.0);
    float fineStep = max(minorStep * 0.5, 1.0);
    float majorStep = minorStep * 5.0;

    float2 fineRatio = fract(fragCoord / fineStep);
    float2 minorRatio = fract(fragCoord / minorStep);
    float2 majorRatio = fract(fragCoord / majorStep);
    float2 fineCell = min(fineRatio, 1.0 - fineRatio) * fineStep;
    float2 minorCell = min(minorRatio, 1.0 - minorRatio) * minorStep;
    float2 majorCell = min(majorRatio, 1.0 - majorRatio) * majorStep;
    float fineLine = 1.0 - smoothstep(0.45, 1.00, min(fineCell.x, fineCell.y));
    float minorLine = 1.0 - smoothstep(0.55, 1.35, min(minorCell.x, minorCell.y));
    float majorLine = 1.0 - smoothstep(0.65, 1.80, min(majorCell.x, majorCell.y));

    float luminance = dot(baseColor, float3(0.299, 0.587, 0.114));
    float3 lightGrid = float3(0.88, 0.93, 1.0);
    float3 darkGrid = float3(0.22, 0.28, 0.36);
    float3 gridColor = mix(lightGrid, darkGrid, step(0.58, luminance));
    float alpha = max(fineLine * 0.08 * gridParams.z,
                      max(minorLine * 0.16, majorLine * 0.28)) * gridParams.x;
    return mix(baseColor, gridColor, alpha);
}

fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    return float4(applyViewportGrid(in.color, in.position.xy, in.gridParams), 1.0);
}
)";

const char* const kMetalLineShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
};

struct Uniforms {
    float4x4 mvp;
    float4 color;
};

struct VertexOut {
    float4 position [[position]];
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]])
{
    return uniforms.color;
}
)";

const char* const kMetalPickShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 pickColor [[attribute(1)]];
};

struct Uniforms {
    float4x4 mvp;
    float4 color;
};

struct VertexOut {
    float4 position [[position]];
    float3 pickColor;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.pickColor = in.pickColor;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]])
{
    return float4(in.pickColor, 1.0);
}
)";
