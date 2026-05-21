#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec3 inPickColor;

layout(location = 0) out vec3 vPickColor;

layout(push_constant) uniform PickPushConstants {
    mat4 mvp;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vPickColor = inPickColor;
}
