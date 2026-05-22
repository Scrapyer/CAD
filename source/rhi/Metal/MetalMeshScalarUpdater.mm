#include "MetalMeshScalarUpdater.h"

#include "MetalShaderTypes.h"

#include <algorithm>

#import <Metal/Metal.h>

bool updateMetalMeshVertexScalars(void* vertexBuffer,
                                  int vertexCount,
                                  const std::vector<unsigned int>& scalarSourceIndices,
                                  const std::vector<float>& scalars,
                                  QString& lastError)
{
    id<MTLBuffer> buffer = static_cast<id<MTLBuffer>>(vertexBuffer);
    auto* vertices = static_cast<MetalMeshVertex*>([buffer contents]);
    if (!vertices) {
        lastError = QStringLiteral("Metal mesh vertex buffer is not mappable");
        return false;
    }

    const size_t count = std::min(scalarSourceIndices.size(),
                                  static_cast<size_t>(std::max(0, vertexCount)));
    for (size_t i = 0; i < count; ++i) {
        const unsigned int sourceIndex = scalarSourceIndices[i];
        vertices[i].scalar = sourceIndex < scalars.size() ? scalars[sourceIndex] : 0.0f;
    }
    return true;
}
