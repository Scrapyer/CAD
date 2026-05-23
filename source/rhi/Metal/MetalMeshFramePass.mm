#include "MetalMeshFramePass.h"

#include "MetalUniformUtils.h"

#include <algorithm>
#include <array>
#include <cmath>

#import <Metal/Metal.h>

namespace {

constexpr NSUInteger kAxesViewportSize = 192;

void bindMetalUniforms(id<MTLRenderCommandEncoder> encoder,
                       const MetalMeshFramePassInputs& inputs,
                       const MetalMeshFramePassDraw& draw)
{
    if (inputs.uniformBuffer) {
        id<MTLBuffer> buffer = static_cast<id<MTLBuffer>>(inputs.uniformBuffer);
        const NSUInteger offset = static_cast<NSUInteger>(draw.uniformOffset);
        [encoder setVertexBuffer:buffer offset:offset atIndex:1];
        [encoder setFragmentBuffer:buffer offset:offset atIndex:1];
        return;
    }

    [encoder setVertexBytes:&draw.uniforms length:sizeof(draw.uniforms) atIndex:1];
    [encoder setFragmentBytes:&draw.uniforms length:sizeof(draw.uniforms) atIndex:1];
}

void drawMetalIndexed(id<MTLRenderCommandEncoder> encoder,
                      const MetalMeshFramePassInputs& inputs,
                      const MetalMeshFramePassDraw& draw,
                      MTLPrimitiveType primitiveType)
{
    if (!draw.enabled) {
        return;
    }
    [encoder setRenderPipelineState:static_cast<id<MTLRenderPipelineState>>(draw.pipelineState)];
    [encoder setVertexBuffer:static_cast<id<MTLBuffer>>(draw.vertexBuffer) offset:0 atIndex:0];
    bindMetalUniforms(encoder, inputs, draw);
    [encoder drawIndexedPrimitives:primitiveType
                        indexCount:static_cast<NSUInteger>(draw.indexCount)
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:static_cast<id<MTLBuffer>>(draw.indexBuffer)
                 indexBufferOffset:0];
}

void drawMetalVertices(id<MTLRenderCommandEncoder> encoder,
                       const MetalMeshFramePassInputs& inputs,
                       const MetalMeshFramePassDraw& draw,
                       MTLPrimitiveType primitiveType)
{
    if (!draw.enabled) {
        return;
    }
    [encoder setRenderPipelineState:static_cast<id<MTLRenderPipelineState>>(draw.pipelineState)];
    [encoder setVertexBuffer:static_cast<id<MTLBuffer>>(draw.vertexBuffer) offset:0 atIndex:0];
    bindMetalUniforms(encoder, inputs, draw);
    [encoder drawPrimitives:primitiveType
                vertexStart:0
                vertexCount:static_cast<NSUInteger>(draw.vertexCount)];
}

void drawMetalAxes(id<MTLRenderCommandEncoder> encoder, const MetalMeshFramePassInputs& inputs)
{
    if (!inputs.axes.enabled && !inputs.axesSolid.enabled) {
        return;
    }

    const NSUInteger margin =
        static_cast<NSUInteger>(std::lround(8.0f * inputs.devicePixelRatio));
    const NSUInteger targetAxesSize =
        static_cast<NSUInteger>(std::lround(static_cast<float>(kAxesViewportSize) *
                                            inputs.devicePixelRatio));
    const NSUInteger availableWidth = inputs.drawableSize.width() > static_cast<int>(margin * 2)
        ? static_cast<NSUInteger>(inputs.drawableSize.width()) - margin * 2
        : 0;
    const NSUInteger availableHeight = inputs.drawableSize.height() > static_cast<int>(margin * 2)
        ? static_cast<NSUInteger>(inputs.drawableSize.height()) - margin * 2
        : 0;
    const NSUInteger axesSize =
        std::min<NSUInteger>(targetAxesSize, std::min(availableWidth, availableHeight));
    if (axesSize == 0) {
        return;
    }

    MTLViewport previousViewport = {
        0.0,
        0.0,
        static_cast<double>(inputs.drawableSize.width()),
        static_cast<double>(inputs.drawableSize.height()),
        0.0,
        1.0
    };
    MTLViewport axesViewport = {
        static_cast<double>(margin),
        static_cast<double>(inputs.drawableSize.height()) - static_cast<double>(axesSize + margin),
        static_cast<double>(axesSize),
        static_cast<double>(axesSize),
        0.0,
        1.0
    };
    MTLScissorRect previousScissor = {
        0,
        0,
        static_cast<NSUInteger>(inputs.drawableSize.width()),
        static_cast<NSUInteger>(inputs.drawableSize.height())
    };
    MTLScissorRect axesScissor = {
        margin,
        static_cast<NSUInteger>(inputs.drawableSize.height()) - axesSize - margin,
        axesSize,
        axesSize
    };
    [encoder setDepthStencilState:static_cast<id<MTLDepthStencilState>>(inputs.overlayDepthStencilState)];
    [encoder setViewport:axesViewport];
    [encoder setScissorRect:axesScissor];
    drawMetalVertices(encoder, inputs, inputs.axesSolid, MTLPrimitiveTypeTriangle);
    drawMetalVertices(encoder, inputs, inputs.axes, MTLPrimitiveTypeLine);

    [encoder setViewport:previousViewport];
    [encoder setScissorRect:previousScissor];
    [encoder setDepthStencilState:static_cast<id<MTLDepthStencilState>>(inputs.depthStencilState)];
}

} // namespace

void encodeMetalMeshFramePass(const MetalMeshFramePassInputs& inputs)
{
    id<MTLRenderCommandEncoder> encoder = static_cast<id<MTLRenderCommandEncoder>>(inputs.encoder);

    [encoder setDepthStencilState:static_cast<id<MTLDepthStencilState>>(inputs.overlayDepthStencilState)];
    [encoder setRenderPipelineState:static_cast<id<MTLRenderPipelineState>>(inputs.backgroundPipelineState)];
    if (inputs.uniformBuffer) {
        [encoder setVertexBuffer:static_cast<id<MTLBuffer>>(inputs.uniformBuffer)
                          offset:static_cast<NSUInteger>(inputs.backgroundUniformOffset)
                         atIndex:0];
    } else {
        [encoder setVertexBytes:&inputs.backgroundUniforms
                         length:sizeof(inputs.backgroundUniforms)
                        atIndex:0];
    }
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

    [encoder setDepthStencilState:static_cast<id<MTLDepthStencilState>>(inputs.depthStencilState)];
    drawMetalIndexed(encoder, inputs, inputs.surface, MTLPrimitiveTypeTriangle);
    drawMetalIndexed(encoder, inputs, inputs.edges, MTLPrimitiveTypeLine);
    drawMetalVertices(encoder, inputs, inputs.points, MTLPrimitiveTypePoint);
    drawMetalIndexed(encoder, inputs, inputs.isoSurface, MTLPrimitiveTypeTriangle);
    drawMetalIndexed(encoder, inputs, inputs.clipPreview, MTLPrimitiveTypeTriangle);
    drawMetalVertices(encoder, inputs, inputs.overlay, MTLPrimitiveTypeLine);
    drawMetalVertices(encoder, inputs, inputs.slice, MTLPrimitiveTypeLine);
    drawMetalVertices(encoder, inputs, inputs.clipPreviewLines, MTLPrimitiveTypeLine);
    drawMetalVertices(encoder, inputs, inputs.selection, MTLPrimitiveTypeLine);
    drawMetalAxes(encoder, inputs);
}
