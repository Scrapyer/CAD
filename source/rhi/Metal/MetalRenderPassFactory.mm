#include "MetalRenderPassFactory.h"

#import <Metal/Metal.h>

void* createMetalColorClearRenderPassDescriptor(void* colorTexture,
                                                float red,
                                                float green,
                                                float blue,
                                                float alpha)
{
    MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    passDescriptor.colorAttachments[0].texture = static_cast<id<MTLTexture>>(colorTexture);
    passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(red, green, blue, alpha);
    return passDescriptor;
}

void* createMetalDrawableMeshRenderPassDescriptor(void* colorTexture,
                                                  void* depthTexture,
                                                  float red,
                                                  float green,
                                                  float blue,
                                                  float alpha)
{
    MTLRenderPassDescriptor* passDescriptor = static_cast<MTLRenderPassDescriptor*>(
        createMetalColorClearRenderPassDescriptor(colorTexture, red, green, blue, alpha));
    passDescriptor.depthAttachment.texture = static_cast<id<MTLTexture>>(depthTexture);
    passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
    passDescriptor.depthAttachment.storeAction = MTLStoreActionDontCare;
    passDescriptor.depthAttachment.clearDepth = 1.0;
    return passDescriptor;
}

void* createMetalPickRenderPassDescriptor(void* colorTexture, void* depthTexture)
{
    MTLRenderPassDescriptor* passDescriptor =
        static_cast<MTLRenderPassDescriptor*>(createMetalDrawableMeshRenderPassDescriptor(
            colorTexture, depthTexture, 0.0f, 0.0f, 0.0f, 1.0f));
    return passDescriptor;
}
