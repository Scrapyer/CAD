#version 450

layout(location = 0) out float vScalar;

struct SourceVertex {
    vec4 position;
    vec4 normal;
    vec4 colorScalar;
};

layout(push_constant) uniform PointPushConstants {
    mat4 mvp;
    vec4 color;
    vec4 contour;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer SourceVertexBuffer {
    SourceVertex sourceVertices[];
} sourceVertexBuffer;

void main()
{
    uint sourceIndex = uint(gl_VertexIndex);
    SourceVertex source = sourceVertexBuffer.sourceVertices[sourceIndex];

    gl_Position = pc.mvp * vec4(source.position.xyz, 1.0);
    vScalar = pc.contour.w > 0.5 ? source.colorScalar.w : 0.0;
    gl_PointSize = 4.0;
}
