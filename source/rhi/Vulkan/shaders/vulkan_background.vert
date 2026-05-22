#version 450

layout(location = 0) out vec3 vColor;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

vec3 bottomColor = vec3(0.68, 0.74, 0.82);
vec3 topColor = vec3(0.38, 0.45, 0.58);

void main()
{
    vec2 pos = positions[gl_VertexIndex];
    float t = clamp(pos.y * 0.5 + 0.5, 0.0, 1.0);
    vColor = mix(bottomColor, topColor, t);
    gl_Position = vec4(pos, 0.0, 1.0);
}
