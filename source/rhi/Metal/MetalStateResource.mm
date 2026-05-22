#include "MetalStateResource.h"

#import <Foundation/Foundation.h>

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

MetalStateResource::~MetalStateResource()
{
    destroy();
}

void MetalStateResource::adopt(void* state)
{
    destroy();
    state_ = state;
}

void MetalStateResource::destroy()
{
    if (state_) {
        releaseObjectiveCObject(static_cast<id>(state_));
        state_ = nullptr;
    }
}
