#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform LinePushConstants {
    mat4 mvp;
    vec4 color;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
