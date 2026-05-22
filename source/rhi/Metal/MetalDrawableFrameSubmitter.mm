#include "MetalDrawableFrameSubmitter.h"

#include "MetalRenderPassFactory.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

bool submitMetalDrawableFrame(void* metalLayer,
                              void* commandQueue,
                              void* depthTexture,
                              float red,
                              float green,
                              float blue,
                              float alpha,
                              const MetalMeshFramePassInputs& framePass,
                              QString& lastError)
{
    CAMetalLayer* layer = static_cast<CAMetalLayer*>(metalLayer);
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) {
        lastError = QStringLiteral("Metal layer did not provide a drawable");
        return false;
    }

    MTLRenderPassDescriptor* passDescriptor = static_cast<MTLRenderPassDescriptor*>(
        createMetalDrawableMeshRenderPassDescriptor(drawable.texture,
                                                    depthTexture,
                                                    red,
                                                    green,
                                                    blue,
                                                    alpha));

    id<MTLCommandBuffer> commandBuffer =
        [static_cast<id<MTLCommandQueue>>(commandQueue) commandBuffer];
    if (!commandBuffer) {
        lastError = QStringLiteral("Metal command buffer creation failed");
        return false;
    }

    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    if (!encoder) {
        lastError = QStringLiteral("Metal render command encoder creation failed");
        return false;
    }

    MetalMeshFramePassInputs mutableFramePass = framePass;
    mutableFramePass.encoder = encoder;
    encodeMetalMeshFramePass(mutableFramePass);
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
    return true;
}
