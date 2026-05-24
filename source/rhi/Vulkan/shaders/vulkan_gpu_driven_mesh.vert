#version 450

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vPosition;
layout(location = 2) out vec3 vColor;
layout(location = 3) out float vScalar;

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

struct PartState {
    uint visible;
    float colorR;
    float colorG;
    float colorB;
};

layout(push_constant) uniform MeshPushConstants {
    mat4 mvp;
    vec4 contour;
    uint useVertexColor;
    uint partStateCount;
    uint pad0;
    uint pad1;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer SourceVertexBuffer {
    SourceVertex sourceVertices[];
} sourceVertexBuffer;

layout(std430, set = 0, binding = 1) readonly buffer TriangleMetaBuffer {
    TriangleMeta triangles[];
} triangleMetaBuffer;

layout(std430, set = 0, binding = 2) readonly buffer PartStateBuffer {
    PartState parts[];
} partStateBuffer;

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

vec3 colorForTriangle(SourceVertex source, int partId)
{
    if (pc.useVertexColor != 0u ||
        partId < 0 ||
        uint(partId) >= pc.partStateCount) {
        return source.colorScalar.xyz;
    }

    PartState part = partStateBuffer.parts[uint(partId)];
    return vec3(part.colorR, part.colorG, part.colorB);
}

void main()
{
    uint cornerId = uint(gl_VertexIndex);
    uint triangleIndex = cornerId / 3u;
    uint corner = cornerId - triangleIndex * 3u;
    TriangleMeta meta = triangleMetaBuffer.triangles[triangleIndex];
    SourceVertex source =
        sourceVertexBuffer.sourceVertices[sourceIndexForCorner(meta, corner)];

    vec3 position = source.position.xyz;
    gl_Position = pc.mvp * vec4(position, 1.0);
    vNormal = normalize(source.normal.xyz);
    vPosition = position;
    vColor = colorForTriangle(source, meta.ids.y);
    vScalar = pc.contour.w > 0.5 ? source.colorScalar.w : 0.0;
}
