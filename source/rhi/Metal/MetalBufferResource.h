#pragma once

#include <QString>

#include <cstddef>

/**
 * @brief Metal buffer 资源封装。
 *
 * 当前用于 Metal 后端中的几何与 readback buffer，集中处理 Objective-C 对象释放。
 */
class MetalBufferResource {
public:
    MetalBufferResource() = default;
    ~MetalBufferResource();

    MetalBufferResource(const MetalBufferResource&) = delete;
    MetalBufferResource& operator=(const MetalBufferResource&) = delete;

    bool upload(void* device,
                const void* data,
                size_t sizeBytes,
                const QString& label,
                QString& lastError);
    bool allocate(void* device, size_t sizeBytes, const QString& label, QString& lastError);
    void destroy();

    bool isValid() const { return buffer_ != nullptr; }
    void* handle() const { return buffer_; }
    size_t sizeBytes() const { return sizeBytes_; }

private:
    void* buffer_ = nullptr;
    size_t sizeBytes_ = 0;
};
