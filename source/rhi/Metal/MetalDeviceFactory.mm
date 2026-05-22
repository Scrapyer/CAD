#include "MetalDeviceFactory.h"

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

bool isMetalSystemDeviceAvailable()
{
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        const bool available = device != nil;
        releaseMetalDevice(device);
        return available;
    }
}

MetalDeviceContext createMetalDeviceContext()
{
    MetalDeviceContext context;
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            context.error = QStringLiteral("Metal default device is unavailable");
            context.info.renderer = QStringLiteral("Metal unavailable");
            context.info.version = context.error;
            context.info.shadingLanguageVersion = QStringLiteral("MSL");
            context.info.vendor = QStringLiteral("Unknown");
            return context;
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        if (!commandQueue) {
            context.error = QStringLiteral("Metal command queue creation failed");
            context.info.renderer = nsStringToQString([device name]);
            context.info.version = context.error;
            context.info.shadingLanguageVersion = QStringLiteral("MSL");
            context.info.vendor = QStringLiteral("Apple");
            releaseMetalDevice(device);
            return context;
        }

        context.info.renderer = nsStringToQString([device name]);
        context.info.version = QStringLiteral("Metal");
        context.info.shadingLanguageVersion = QStringLiteral("MSL");
        context.info.vendor = QStringLiteral("Apple");
        context.device = device;
        context.commandQueue = commandQueue;
    }
    return context;
}
