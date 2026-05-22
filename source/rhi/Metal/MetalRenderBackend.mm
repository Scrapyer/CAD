#include "MetalRenderBackend.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

namespace {

QString nsStringToQString(NSString* value)
{
    if (!value) {
        return QString();
    }
    return QString::fromUtf8([value UTF8String]);
}

void releaseMetalDevice(id<MTLDevice> device)
{
#if !__has_feature(objc_arc)
    [device release];
#else
    (void)device;
#endif
}

} // namespace

bool MetalRenderBackend::isSystemAvailable()
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        const bool available = device != nil;
        releaseMetalDevice(device);
        return available;
    }
}

void MetalRenderBackend::initialize()
{
    if (initialized_) {
        return;
    }

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            lastError_ = QStringLiteral("Metal default device is unavailable");
            info_.renderer = QStringLiteral("Metal unavailable");
            info_.version = lastError_;
            info_.shadingLanguageVersion = QStringLiteral("MSL");
            info_.vendor = QStringLiteral("Unknown");
            initialized_ = true;
            return;
        }

        info_.renderer = nsStringToQString([device name]);
        info_.version = QStringLiteral("Metal");
        info_.shadingLanguageVersion = QStringLiteral("MSL");
        info_.vendor = QStringLiteral("Apple");
        lastError_.clear();
        initialized_ = true;
        releaseMetalDevice(device);
    }
}
