#include "MetalPickUtils.h"

#include <QString>

#import <Metal/Metal.h>

QVector3D metalIdToPickColor(int id)
{
    if (id < 0) {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }
    const int encoded = id + 1;
    const int red = encoded & 0xFF;
    const int green = (encoded >> 8) & 0xFF;
    const int blue = (encoded >> 16) & 0xFF;
    return QVector3D(static_cast<float>(red) / 255.0f,
                     static_cast<float>(green) / 255.0f,
                     static_cast<float>(blue) / 255.0f);
}

int metalPickColorToId(const uint8_t* pixel)
{
    const int encoded = static_cast<int>(pixel[0]) |
        (static_cast<int>(pixel[1]) << 8) |
        (static_cast<int>(pixel[2]) << 16);
    return encoded > 0 ? encoded - 1 : -1;
}

void clearMetalReadbackPixel(void* readbackBuffer)
{
    id<MTLBuffer> buffer = static_cast<id<MTLBuffer>>(readbackBuffer);
    uint8_t* bytes = static_cast<uint8_t*>([buffer contents]);
    if (!bytes) {
        return;
    }
    bytes[0] = 0;
    bytes[1] = 0;
    bytes[2] = 0;
    bytes[3] = 0;
}

bool readMetalPickElementId(void* readbackBuffer, int& elementId, QString& lastError)
{
    id<MTLBuffer> buffer = static_cast<id<MTLBuffer>>(readbackBuffer);
    const uint8_t* bytes = static_cast<const uint8_t*>([buffer contents]);
    if (!bytes) {
        lastError = QStringLiteral("Metal pick readback buffer is not mappable");
        return false;
    }
    elementId = metalPickColorToId(bytes);
    return true;
}
