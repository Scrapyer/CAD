#include "MetalBufferResource.h"

#import <Metal/Metal.h>

namespace {

void releaseObjectiveCObject(id object)
{
#if !__has_feature(objc_arc)
    [object release];
#else
    (void)object;
#endif
}

} // namespace

MetalBufferResource::~MetalBufferResource()
{
    destroy();
}

bool MetalBufferResource::upload(void* device,
                                 const void* data,
                                 size_t sizeBytes,
                                 const QString& label,
                                 QString& lastError)
{
    destroy();
    if (sizeBytes == 0) {
        return true;
    }
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }
    if (!data) {
        lastError = QStringLiteral("Metal %1 buffer upload data is null").arg(label);
        return false;
    }

    @autoreleasepool {
        buffer_ = [static_cast<id<MTLDevice>>(device) newBufferWithBytes:data
                                                                  length:static_cast<NSUInteger>(sizeBytes)
                                                                 options:MTLResourceStorageModeShared];
        if (!buffer_) {
            lastError = QStringLiteral("Metal %1 buffer creation failed").arg(label);
            sizeBytes_ = 0;
            return false;
        }
        sizeBytes_ = sizeBytes;
    }
    return true;
}

bool MetalBufferResource::allocate(void* device,
                                   size_t sizeBytes,
                                   const QString& label,
                                   QString& lastError)
{
    destroy();
    if (sizeBytes == 0) {
        return true;
    }
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }

    @autoreleasepool {
        buffer_ = [static_cast<id<MTLDevice>>(device) newBufferWithLength:static_cast<NSUInteger>(sizeBytes)
                                                                   options:MTLResourceStorageModeShared];
        if (!buffer_) {
            lastError = QStringLiteral("Metal %1 buffer allocation failed").arg(label);
            sizeBytes_ = 0;
            return false;
        }
        sizeBytes_ = sizeBytes;
    }
    return true;
}

void MetalBufferResource::destroy()
{
    if (buffer_) {
        releaseObjectiveCObject(static_cast<id<MTLBuffer>>(buffer_));
        buffer_ = nullptr;
    }
    sizeBytes_ = 0;
}
