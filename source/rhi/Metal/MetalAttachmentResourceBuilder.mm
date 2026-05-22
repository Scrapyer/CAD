#include "MetalAttachmentResourceBuilder.h"

#include "MetalDepthStencilFactory.h"

#import <Metal/Metal.h>

namespace {

bool validateMetalAttachmentInputs(void* device, const QSize& drawableSize, QString& lastError)
{
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }
    if (drawableSize.isEmpty()) {
        lastError = QStringLiteral("Metal drawable size is empty");
        return false;
    }
    return true;
}

} // namespace

bool ensureMetalFrameAttachmentResources(void* device,
                                         const QSize& drawableSize,
                                         MetalStateResource& depthStencilState,
                                         MetalStateResource& overlayDepthStencilState,
                                         MetalTextureResource& depthTexture,
                                         QString& lastError)
{
    if (!validateMetalAttachmentInputs(device, drawableSize, lastError)) {
        return false;
    }

    @autoreleasepool {
        if (!depthStencilState.isValid()) {
            void* state = nullptr;
            const MetalDepthStencilConfig config{
                static_cast<unsigned long>(MTLCompareFunctionLessEqual),
                true,
                QStringLiteral("depth")
            };
            if (!createMetalDepthStencilState(device, config, state, lastError)) {
                return false;
            }
            depthStencilState.adopt(state);
        }
        if (!overlayDepthStencilState.isValid()) {
            void* state = nullptr;
            const MetalDepthStencilConfig config{
                static_cast<unsigned long>(MTLCompareFunctionAlways),
                false,
                QStringLiteral("overlay")
            };
            if (!createMetalDepthStencilState(device, config, state, lastError)) {
                return false;
            }
            overlayDepthStencilState.adopt(state);
        }

        if (!depthTexture.isValid()) {
            if (!depthTexture.create2D(device,
                                       MTLPixelFormatDepth32Float,
                                       drawableSize.width(),
                                       drawableSize.height(),
                                       MTLTextureUsageRenderTarget,
                                       MTLStorageModePrivate,
                                       QStringLiteral("depth"),
                                       lastError)) {
                return false;
            }
        }
    }

    return true;
}

bool ensureMetalPickAttachmentResources(void* device,
                                        const QSize& drawableSize,
                                        MetalTextureResource& pickColorTexture,
                                        MetalTextureResource& pickDepthTexture,
                                        MetalBufferResource& pickReadbackBuffer,
                                        QString& lastError)
{
    if (!validateMetalAttachmentInputs(device, drawableSize, lastError)) {
        return false;
    }

    @autoreleasepool {
        if (!pickColorTexture.isValid()) {
            if (!pickColorTexture.create2D(device,
                                           MTLPixelFormatRGBA8Unorm,
                                           drawableSize.width(),
                                           drawableSize.height(),
                                           MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                           MTLStorageModePrivate,
                                           QStringLiteral("pick color"),
                                           lastError)) {
                return false;
            }
        }

        if (!pickDepthTexture.isValid()) {
            if (!pickDepthTexture.create2D(device,
                                           MTLPixelFormatDepth32Float,
                                           drawableSize.width(),
                                           drawableSize.height(),
                                           MTLTextureUsageRenderTarget,
                                           MTLStorageModePrivate,
                                           QStringLiteral("pick depth"),
                                           lastError)) {
                return false;
            }
        }

        if (!pickReadbackBuffer.isValid()) {
            if (!pickReadbackBuffer.allocate(device,
                                             4,
                                             QStringLiteral("pick readback"),
                                             lastError)) {
                return false;
            }
        }
    }

    return true;
}
