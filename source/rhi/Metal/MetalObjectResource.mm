#include "MetalObjectResource.h"

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

id retainObjectiveCObject(id object)
{
#if !__has_feature(objc_arc)
    return [object retain];
#else
    return object;
#endif
}

} // namespace

MetalObjectResource::~MetalObjectResource()
{
    destroy();
}

void MetalObjectResource::adopt(void* object)
{
    destroy();
    object_ = object;
}

void MetalObjectResource::retain(void* object)
{
    destroy();
    object_ = retainObjectiveCObject(static_cast<id>(object));
}

void MetalObjectResource::destroy()
{
    if (object_) {
        releaseObjectiveCObject(static_cast<id>(object_));
        object_ = nullptr;
    }
}
