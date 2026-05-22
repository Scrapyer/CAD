#include "MetalBufferResource.h"

#include <vector>

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

MetalBufferResource::~MetalBufferResource()
{
    destroy();
}

bool MetalBufferResource::upload(void* device,
                                 const void* data,
                                 size_t sizeBytes,
                                 const QString& label,
                                 QString& lastError)
{
    destroy();
    if (sizeBytes == 0) {
        return true;
    }
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }
    if (!data) {
        lastError = QStringLiteral("Metal %1 buffer upload data is null").arg(label);
        return false;
    }

    @autoreleasepool {
        buffer_ = [static_cast<id<MTLDevice>>(device) newBufferWithBytes:data
                                                                  length:static_cast<NSUInteger>(sizeBytes)
                                                                 options:MTLResourceStorageModeShared];
        if (!buffer_) {
            lastError = QStringLiteral("Metal %1 buffer creation failed").arg(label);
            sizeBytes_ = 0;
            return false;
        }
        sizeBytes_ = sizeBytes;
        hostVisible_ = true;
    }
    return true;
}

bool MetalBufferResource::uploadPrivate(void* device,
                                        void* commandQueue,
                                        const void* data,
                                        size_t sizeBytes,
                                        const QString& label,
                                        QString& lastError)
{
    const std::vector<MetalPrivateBufferUpload> upload = {
        MetalPrivateBufferUpload{this, data, sizeBytes, label}
    };
    return uploadPrivateBatch(device, commandQueue, upload, lastError);
}

bool MetalBufferResource::uploadPrivateBatch(void* device,
                                             void* commandQueue,
                                             const std::vector<MetalPrivateBufferUpload>& uploads,
                                             QString& lastError)
{
    if (!device || !commandQueue) {
        lastError = QStringLiteral("Metal device or command queue is not initialized");
        return false;
    }

    @autoreleasepool {
        struct PendingUpload {
            MetalBufferResource* target = nullptr;
            id<MTLBuffer> staging = nil;
            id<MTLBuffer> privateBuffer = nil;
            size_t sizeBytes = 0;
            QString label;
        };

        auto cleanup = [](std::vector<PendingUpload>& pending) {
            for (PendingUpload& upload : pending) {
                if (upload.privateBuffer) {
                    releaseObjectiveCObject(upload.privateBuffer);
                    upload.privateBuffer = nil;
                }
                if (upload.staging) {
                    releaseObjectiveCObject(upload.staging);
                    upload.staging = nil;
                }
            }
        };

        for (const MetalPrivateBufferUpload& upload : uploads) {
            if (!upload.target) {
                lastError = QStringLiteral("Metal private upload target is null");
                return false;
            }
            upload.target->destroy();
        }

        id<MTLDevice> metalDevice = static_cast<id<MTLDevice>>(device);
        id<MTLCommandQueue> queue = static_cast<id<MTLCommandQueue>>(commandQueue);
        std::vector<PendingUpload> pending;
        pending.reserve(uploads.size());

        for (const MetalPrivateBufferUpload& upload : uploads) {
            if (upload.sizeBytes == 0) {
                continue;
            }
            if (!upload.data) {
                cleanup(pending);
                lastError = QStringLiteral("Metal %1 private upload data is null").arg(upload.label);
                return false;
            }

            PendingUpload pendingUpload;
            pendingUpload.target = upload.target;
            pendingUpload.sizeBytes = upload.sizeBytes;
            pendingUpload.label = upload.label;
            pendingUpload.staging =
                [metalDevice newBufferWithBytes:upload.data
                                         length:static_cast<NSUInteger>(upload.sizeBytes)
                                        options:MTLResourceStorageModeShared];
            if (!pendingUpload.staging) {
                cleanup(pending);
                lastError = QStringLiteral("Metal %1 staging buffer creation failed").arg(upload.label);
                return false;
            }

            pendingUpload.privateBuffer =
                [metalDevice newBufferWithLength:static_cast<NSUInteger>(upload.sizeBytes)
                                         options:MTLResourceStorageModePrivate];
            if (!pendingUpload.privateBuffer) {
                cleanup(pending);
                releaseObjectiveCObject(pendingUpload.staging);
                lastError = QStringLiteral("Metal %1 private buffer creation failed").arg(upload.label);
                return false;
            }
            pending.push_back(pendingUpload);
        }

        if (pending.empty()) {
            return true;
        }

        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (!commandBuffer) {
            cleanup(pending);
            lastError = QStringLiteral("Metal private batch upload command buffer creation failed");
            return false;
        }

        id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
        if (!blitEncoder) {
            cleanup(pending);
            lastError = QStringLiteral("Metal private batch upload blit encoder creation failed");
            return false;
        }

        for (const PendingUpload& upload : pending) {
            [blitEncoder copyFromBuffer:upload.staging
                            sourceOffset:0
                                toBuffer:upload.privateBuffer
                       destinationOffset:0
                                    size:static_cast<NSUInteger>(upload.sizeBytes)];
        }
        [blitEncoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
        if (commandBuffer.status == MTLCommandBufferStatusError) {
            cleanup(pending);
            lastError = QStringLiteral("Metal private batch upload failed");
            return false;
        }

        for (PendingUpload& upload : pending) {
            upload.target->buffer_ = upload.privateBuffer;
            upload.target->sizeBytes_ = upload.sizeBytes;
            upload.target->hostVisible_ = false;
            upload.privateBuffer = nil;
            releaseObjectiveCObject(upload.staging);
            upload.staging = nil;
        }
    }
    return true;
}

bool MetalBufferResource::allocate(void* device,
                                   size_t sizeBytes,
                                   const QString& label,
                                   QString& lastError)
{
    destroy();
    if (sizeBytes == 0) {
        return true;
    }
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }

    @autoreleasepool {
        buffer_ = [static_cast<id<MTLDevice>>(device) newBufferWithLength:static_cast<NSUInteger>(sizeBytes)
                                                                   options:MTLResourceStorageModeShared];
        if (!buffer_) {
            lastError = QStringLiteral("Metal %1 buffer allocation failed").arg(label);
            sizeBytes_ = 0;
            return false;
        }
        sizeBytes_ = sizeBytes;
        hostVisible_ = true;
    }
    return true;
}

void MetalBufferResource::destroy()
{
    if (buffer_) {
        releaseObjectiveCObject(static_cast<id<MTLBuffer>>(buffer_));
        buffer_ = nullptr;
    }
    sizeBytes_ = 0;
    hostVisible_ = false;
}
