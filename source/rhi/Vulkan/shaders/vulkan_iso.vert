#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

layout(push_constant) uniform IsoPushConstants {
    mat4 mvp;
    vec4 color;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vNormal = normalize(inNormal);
    vColor = inColor;
}
