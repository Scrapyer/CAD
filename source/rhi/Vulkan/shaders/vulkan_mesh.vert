#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vPosition;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vScalar;

layout(push_constant) uniform MeshPushConstants {
    mat4 mvp;
    vec4 contour;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer ScalarBuffer {
    float scalars[];
} scalarBuffer;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vNormal = normalize(inNormal);
    vPosition = inPosition;
    vColor = inColor;
    vScalar = pc.contour.w > 0.5 ? scalarBuffer.scalars[gl_VertexIndex] : 0.0;
}
