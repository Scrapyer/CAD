#version 450

layout(location = 0) out vec3 vColor;

layout(push_constant) uniform BackgroundPushConstants {
    vec4 bottomColor;
    vec4 topColor;
} pc;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

void main()
{
    vec2 pos = positions[gl_VertexIndex];
    float t = clamp(pos.y * 0.5 + 0.5, 0.0, 1.0);
    vColor = mix(pc.bottomColor.rgb, pc.topColor.rgb, t);
    gl_Position = vec4(pos, 0.0, 1.0);
}
