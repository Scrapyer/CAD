#include "MetalPickPass.h"

#include "MetalPickUtils.h"
#include "MetalRenderPassFactory.h"

#import <Metal/Metal.h>

bool executeMetalPickPass(const MetalPickPassInputs& inputs, int& elementId, QString& lastError)
{
    id<MTLTexture> colorTexture = static_cast<id<MTLTexture>>(inputs.colorTexture);
    id<MTLTexture> depthTexture = static_cast<id<MTLTexture>>(inputs.depthTexture);
    id<MTLBuffer> readbackBuffer = static_cast<id<MTLBuffer>>(inputs.readbackBuffer);
    clearMetalReadbackPixel(readbackBuffer);

    MTLRenderPassDescriptor* passDescriptor = static_cast<MTLRenderPassDescriptor*>(
        createMetalPickRenderPassDescriptor(colorTexture, depthTexture));

    id<MTLCommandBuffer> commandBuffer =
        [static_cast<id<MTLCommandQueue>>(inputs.commandQueue) commandBuffer];
    if (!commandBuffer) {
        lastError = QStringLiteral("Metal pick command buffer creation failed");
        return false;
    }

    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:passDescriptor];
    if (!encoder) {
        lastError = QStringLiteral("Metal pick render command encoder creation failed");
        return false;
    }

    [encoder setDepthStencilState:static_cast<id<MTLDepthStencilState>>(inputs.depthStencilState)];
    [encoder setRenderPipelineState:static_cast<id<MTLRenderPipelineState>>(inputs.pickPipelineState)];
    [encoder setVertexBuffer:static_cast<id<MTLBuffer>>(inputs.meshVertexBuffer) offset:0 atIndex:0];
    [encoder setVertexBytes:&inputs.uniforms length:sizeof(inputs.uniforms) atIndex:1];
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:static_cast<NSUInteger>(inputs.meshIndexCount)
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:static_cast<id<MTLBuffer>>(inputs.meshIndexBuffer)
                 indexBufferOffset:0];
    [encoder endEncoding];

    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
    if (!blitEncoder) {
        lastError = QStringLiteral("Metal pick blit command encoder creation failed");
        return false;
    }
    [blitEncoder copyFromTexture:colorTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(inputs.pickX),
                                               static_cast<NSUInteger>(inputs.pickY),
                                               0)
                      sourceSize:MTLSizeMake(1, 1, 1)
                        toBuffer:readbackBuffer
               destinationOffset:0
          destinationBytesPerRow:4
        destinationBytesPerImage:4];
    [blitEncoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if ([commandBuffer status] == MTLCommandBufferStatusError) {
        lastError = QStringLiteral("Metal pick command buffer execution failed");
        return false;
    }

    return readMetalPickElementId(readbackBuffer, elementId, lastError);
}
