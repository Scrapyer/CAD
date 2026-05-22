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
    if (!buffer.upload(device,
                       lineVertices.data(),
                       lineVertices.size() * sizeof(float),
                       label,
                       lastError)) {
        buffer.destroy();
        vertexCount = 0;
        return false;
    }
    return true;
}
