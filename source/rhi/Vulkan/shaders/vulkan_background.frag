#version 450

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform BackgroundPushConstants {
    vec4 bottomColor;
    vec4 topColor;
    vec4 gridParams;
} pc;

vec3 applyViewportGrid(vec3 baseColor, vec2 fragCoord)
{
    float minorStep = max(pc.gridParams.y, 1.0);
    float fineStep = max(minorStep * 0.5, 1.0);
    float majorStep = minorStep * 5.0;

    vec2 fineCell = min(fract(fragCoord / fineStep), 1.0 - fract(fragCoord / fineStep)) * fineStep;
    vec2 minorCell = min(fract(fragCoord / minorStep), 1.0 - fract(fragCoord / minorStep)) * minorStep;
    vec2 majorCell = min(fract(fragCoord / majorStep), 1.0 - fract(fragCoord / majorStep)) * majorStep;
    float fineLine = 1.0 - smoothstep(0.45, 1.00, min(fineCell.x, fineCell.y));
    float minorLine = 1.0 - smoothstep(0.55, 1.35, min(minorCell.x, minorCell.y));
    float majorLine = 1.0 - smoothstep(0.65, 1.80, min(majorCell.x, majorCell.y));

    vec3 gridColor = vec3(0.50, 0.58, 0.68);
    float alpha = max(fineLine * pc.gridParams.z,
                      max(minorLine, majorLine)) * pc.gridParams.x;
    return mix(baseColor, gridColor, clamp(alpha, 0.0, 1.0));
}

void main()
{
    outColor = vec4(applyViewportGrid(vColor, gl_FragCoord.xy), 1.0);
}
