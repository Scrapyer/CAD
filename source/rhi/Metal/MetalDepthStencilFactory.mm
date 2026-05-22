#include "MetalDepthStencilFactory.h"

#import <Foundation/Foundation.h>
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

bool createMetalDepthStencilState(void* device,
                                  const MetalDepthStencilConfig& config,
                                  void*& depthStencilState,
                                  QString& lastError)
{
    depthStencilState = nullptr;
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }

    @autoreleasepool {
        MTLDepthStencilDescriptor* descriptor = [[MTLDepthStencilDescriptor alloc] init];
        descriptor.depthCompareFunction = static_cast<MTLCompareFunction>(config.compareFunction);
        descriptor.depthWriteEnabled = config.depthWriteEnabled ? YES : NO;
        id<MTLDepthStencilState> state =
            [static_cast<id<MTLDevice>>(device) newDepthStencilStateWithDescriptor:descriptor];
        releaseObjectiveCObject(descriptor);
        if (!state) {
            lastError = QStringLiteral("Metal %1 depth stencil state creation failed").arg(config.label);
            return false;
        }
        depthStencilState = state;
    }
    return true;
}
