#include "MetalPipelineFactory.h"

#include "MetalShaderTypes.h"

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

QString nsStringToQString(NSString* value)
{
    if (!value) {
        return QString();
    }
    return QString::fromUtf8([value UTF8String]);
}

QString nsErrorToQString(NSError* error)
{
    if (!error) {
        return QString();
    }
    return nsStringToQString([error localizedDescription]);
}

MTLVertexDescriptor* createVertexDescriptor(MetalPipelineVertexLayout layout)
{
    if (layout == MetalPipelineVertexLayout::None) {
        return nil;
    }

    MTLVertexDescriptor* descriptor = [[MTLVertexDescriptor alloc] init];
    switch (layout) {
    case MetalPipelineVertexLayout::Mesh:
        descriptor.attributes[0].format = MTLVertexFormatFloat3;
        descriptor.attributes[0].offset = offsetof(MetalMeshVertex, position);
        descriptor.attributes[0].bufferIndex = 0;
        descriptor.attributes[1].format = MTLVertexFormatFloat3;
        descriptor.attributes[1].offset = offsetof(MetalMeshVertex, normal);
        descriptor.attributes[1].bufferIndex = 0;
        descriptor.attributes[2].format = MTLVertexFormatFloat3;
        descriptor.attributes[2].offset = offsetof(MetalMeshVertex, color);
        descriptor.attributes[2].bufferIndex = 0;
        descriptor.attributes[3].format = MTLVertexFormatFloat;
        descriptor.attributes[3].offset = offsetof(MetalMeshVertex, scalar);
        descriptor.attributes[3].bufferIndex = 0;
        descriptor.layouts[0].stride = sizeof(MetalMeshVertex);
        descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        break;
    case MetalPipelineVertexLayout::Line:
        descriptor.attributes[0].format = MTLVertexFormatFloat3;
        descriptor.attributes[0].offset = offsetof(MetalLineVertex, position);
        descriptor.attributes[0].bufferIndex = 0;
        descriptor.attributes[1].format = MTLVertexFormatFloat;
        descriptor.attributes[1].offset = offsetof(MetalLineVertex, scalar);
        descriptor.attributes[1].bufferIndex = 0;
        descriptor.layouts[0].stride = kMetalLineVertexStride;
        descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        break;
    case MetalPipelineVertexLayout::Pick:
        descriptor.attributes[0].format = MTLVertexFormatFloat3;
        descriptor.attributes[0].offset = offsetof(MetalMeshVertex, position);
        descriptor.attributes[0].bufferIndex = 0;
        descriptor.attributes[1].format = MTLVertexFormatFloat3;
        descriptor.attributes[1].offset = offsetof(MetalMeshVertex, pickColor);
        descriptor.attributes[1].bufferIndex = 0;
        descriptor.layouts[0].stride = sizeof(MetalMeshVertex);
        descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        break;
    case MetalPipelineVertexLayout::None:
        break;
    }
    return descriptor;
}

void configureAlphaBlending(MTLRenderPipelineColorAttachmentDescriptor* attachment)
{
    attachment.blendingEnabled = YES;
    attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    attachment.rgbBlendOperation = MTLBlendOperationAdd;
    attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
    attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    attachment.alphaBlendOperation = MTLBlendOperationAdd;
}

} // namespace

bool createMetalRenderPipelineState(void* device,
                                    const MetalRenderPipelineConfig& config,
                                    void*& pipelineState,
                                    QString& lastError)
{
    pipelineState = nullptr;
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }
    if (!config.shaderSource) {
        lastError = QStringLiteral("Metal %1 shader source is null").arg(config.label);
        return false;
    }

    @autoreleasepool {
        NSError* libraryError = nil;
        NSString* source = [NSString stringWithUTF8String:config.shaderSource];
        id<MTLLibrary> library =
            [static_cast<id<MTLDevice>>(device) newLibraryWithSource:source
                                                             options:nil
                                                               error:&libraryError];
        if (!library) {
            lastError = QStringLiteral("Metal %1 shader compilation failed: ")
                            .arg(config.label) +
                nsErrorToQString(libraryError);
            return false;
        }

        id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertex_main"];
        id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"fragment_main"];
        if (!vertexFunction || !fragmentFunction) {
            lastError = QStringLiteral("Metal %1 shader entry point is missing").arg(config.label);
            releaseObjectiveCObject(vertexFunction);
            releaseObjectiveCObject(fragmentFunction);
            releaseObjectiveCObject(library);
            return false;
        }

        MTLVertexDescriptor* vertexDescriptor = createVertexDescriptor(config.vertexLayout);
        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = vertexFunction;
        descriptor.fragmentFunction = fragmentFunction;
        descriptor.vertexDescriptor = vertexDescriptor;
        descriptor.colorAttachments[0].pixelFormat =
            static_cast<MTLPixelFormat>(config.colorPixelFormat);
        descriptor.depthAttachmentPixelFormat =
            static_cast<MTLPixelFormat>(config.depthPixelFormat);
        if (config.alphaBlending) {
            configureAlphaBlending(descriptor.colorAttachments[0]);
        }

        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> metalPipelineState =
            [static_cast<id<MTLDevice>>(device) newRenderPipelineStateWithDescriptor:descriptor
                                                                               error:&pipelineError];
        releaseObjectiveCObject(descriptor);
        releaseObjectiveCObject(vertexDescriptor);
        releaseObjectiveCObject(vertexFunction);
        releaseObjectiveCObject(fragmentFunction);
        releaseObjectiveCObject(library);

        if (!metalPipelineState) {
            lastError = QStringLiteral("Metal %1 pipeline creation failed: ")
                            .arg(config.label) +
                nsErrorToQString(pipelineError);
            return false;
        }

        pipelineState = metalPipelineState;
    }
    return true;
}

bool ensureMetalRenderPipelineState(void* device,
                                    const MetalRenderPipelineConfig& config,
                                    MetalStateResource& pipelineState,
                                    QString& lastError)
{
    if (pipelineState.isValid()) {
        return true;
    }

    void* handle = nullptr;
    if (!createMetalRenderPipelineState(device, config, handle, lastError)) {
        return false;
    }
    pipelineState.adopt(handle);
    return true;
}
