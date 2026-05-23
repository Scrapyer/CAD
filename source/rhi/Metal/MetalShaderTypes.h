#pragma once

#include <cstddef>

struct MetalMeshUniforms {
    float mvp[16];
    float color[4];
    float contour[4];
};

struct MetalBackgroundUniforms {
    float bottomColor[4];
    float topColor[4];
    float gridParams[4];
};

struct MetalMeshVertex {
    float position[3];
    float normal[3];
    float color[3];
    float pickColor[3];
    float scalar;
};

struct MetalLineVertex {
    float position[3];
    float scalar;
};

constexpr size_t kMetalFloat3ByteSize = sizeof(float) * 3;
constexpr size_t kMetalLineVertexStride = sizeof(MetalLineVertex);

static_assert(sizeof(MetalMeshVertex) == sizeof(float) * 13,
              "MetalMeshVertex must match Metal vertex shader attributes");
static_assert(offsetof(MetalMeshVertex, normal) == kMetalFloat3ByteSize,
              "MetalMeshVertex normal offset must match pipeline descriptor");
static_assert(offsetof(MetalMeshVertex, color) == kMetalFloat3ByteSize * 2,
              "MetalMeshVertex color offset must match pipeline descriptor");
static_assert(offsetof(MetalMeshVertex, pickColor) == kMetalFloat3ByteSize * 3,
              "MetalMeshVertex pickColor offset must match pipeline descriptor");
static_assert(offsetof(MetalMeshVertex, scalar) == sizeof(float) * 12,
              "MetalMeshVertex scalar offset must match pipeline descriptor");
static_assert(sizeof(MetalLineVertex) == sizeof(float) * 4,
              "MetalLineVertex must match Metal line vertex shader attributes");
static_assert(offsetof(MetalLineVertex, scalar) == kMetalFloat3ByteSize,
              "MetalLineVertex scalar offset must match pipeline descriptor");
