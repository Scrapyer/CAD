#version 450

layout(location = 0) out vec3 vPickColor;

struct SourceVertex {
    vec4 position;
    vec4 normal;
    vec4 colorScalar;
};

struct TriangleMeta {
    uvec4 indices;
    ivec4 ids;
    vec4 boundsMin;
    vec4 boundsMax;
};

layout(push_constant) uniform PickPushConstants {
    mat4 mvp;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer SourceVertexBuffer {
    SourceVertex sourceVertices[];
} sourceVertexBuffer;

layout(std430, set = 0, binding = 1) readonly buffer TriangleMetaBuffer {
    TriangleMeta triangles[];
} triangleMetaBuffer;

uint sourceIndexForCorner(TriangleMeta meta, uint corner)
{
    if (corner == 0u) {
        return meta.indices.x;
    }
    if (corner == 1u) {
        return meta.indices.y;
    }
    return meta.indices.z;
}

vec3 idToPickColor(int id)
{
    if (id < 0) {
        return vec3(0.0);
    }

    int encoded = id + 1;
    return vec3(
        float(encoded & 0xFF) / 255.0,
        float((encoded >> 8) & 0xFF) / 255.0,
        float((encoded >> 16) & 0xFF) / 255.0);
}

void main()
{
    uint cornerId = uint(gl_VertexIndex);
    uint triangleIndex = cornerId / 3u;
    uint corner = cornerId - triangleIndex * 3u;
    TriangleMeta meta = triangleMetaBuffer.triangles[triangleIndex];
    SourceVertex source =
        sourceVertexBuffer.sourceVertices[sourceIndexForCorner(meta, corner)];

    gl_Position = pc.mvp * vec4(source.position.xyz, 1.0);
    vPickColor = idToPickColor(meta.ids.x);
}
