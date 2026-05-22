#pragma once

#include "MetalBufferResource.h"
#include "MetalStateResource.h"
#include "MetalTextureResource.h"

#include <QSize>
#include <QString>

bool ensureMetalFrameAttachmentResources(void* device,
                                         const QSize& drawableSize,
                                         MetalStateResource& depthStencilState,
                                         MetalStateResource& overlayDepthStencilState,
                                         MetalTextureResource& depthTexture,
                                         QString& lastError);

bool ensureMetalPickAttachmentResources(void* device,
                                        const QSize& drawableSize,
                                        MetalTextureResource& pickColorTexture,
                                        MetalTextureResource& pickDepthTexture,
                                        MetalBufferResource& pickReadbackBuffer,
                                        QString& lastError);
