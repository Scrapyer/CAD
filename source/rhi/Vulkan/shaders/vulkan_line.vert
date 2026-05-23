#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inScalar;
layout(location = 0) out float vScalar;

layout(push_constant) uniform LinePushConstants {
    mat4 mvp;
    vec4 color;
    vec4 contour;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vScalar = inScalar;
    gl_PointSize = 4.0;
}
