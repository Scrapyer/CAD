#include "MetalTextureResource.h"

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

MetalTextureResource::~MetalTextureResource()
{
    destroy();
}

bool MetalTextureResource::create2D(void* device,
                                    unsigned long pixelFormat,
                                    int width,
                                    int height,
                                    unsigned long usage,
                                    unsigned long storageMode,
                                    const QString& label,
                                    QString& lastError)
{
    destroy();
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }
    if (width <= 0 || height <= 0) {
        lastError = QStringLiteral("Metal %1 texture size is empty").arg(label);
        return false;
    }

    @autoreleasepool {
        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:static_cast<MTLPixelFormat>(pixelFormat)
                                                                width:static_cast<NSUInteger>(width)
                                                               height:static_cast<NSUInteger>(height)
                                                            mipmapped:NO];
        descriptor.usage = static_cast<MTLTextureUsage>(usage);
        descriptor.storageMode = static_cast<MTLStorageMode>(storageMode);
        texture_ = [static_cast<id<MTLDevice>>(device) newTextureWithDescriptor:descriptor];
        if (!texture_) {
            lastError = QStringLiteral("Metal %1 texture creation failed").arg(label);
            width_ = 0;
            height_ = 0;
            return false;
        }
        width_ = width;
        height_ = height;
    }
    return true;
}

void MetalTextureResource::destroy()
{
    if (texture_) {
        releaseObjectiveCObject(static_cast<id<MTLTexture>>(texture_));
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}
