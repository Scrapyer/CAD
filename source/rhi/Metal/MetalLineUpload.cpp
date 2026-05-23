#include "MetalLineUpload.h"

bool uploadMetalLineVertices(void* device,
                             const std::vector<float>& lineVertices,
                             MetalBufferResource& buffer,
                             int& vertexCount,
                             const QString& label,
                             QString& lastError)
{
    buffer.destroy();
    vertexCount = 0;
    if (lineVertices.empty()) {
        return true;
    }
    if (lineVertices.size() % 3 != 0) {
        lastError = QStringLiteral("Metal %1 line vertex array is invalid").arg(label);
        return false;
    }

    vertexCount = static_cast<int>(lineVertices.size() / 3);
    std::vector<MetalLineVertex> vertices(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        vertices[static_cast<size_t>(i)].position[0] = lineVertices[static_cast<size_t>(i) * 3 + 0];
        vertices[static_cast<size_t>(i)].position[1] = lineVertices[static_cast<size_t>(i) * 3 + 1];
        vertices[static_cast<size_t>(i)].position[2] = lineVertices[static_cast<size_t>(i) * 3 + 2];
        vertices[static_cast<size_t>(i)].scalar = 0.0f;
    }
    if (!buffer.upload(device,
                       vertices.data(),
                       vertices.size() * sizeof(MetalLineVertex),
                       label,
                       lastError)) {
        buffer.destroy();
        vertexCount = 0;
        return false;
    }
    return true;
}
