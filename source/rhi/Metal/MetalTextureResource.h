#pragma once

#include <QString>

/**
 * @brief Metal texture 资源封装。
 *
 * 用于 Metal 后端中的 depth attachment 与离屏 pick attachment。
 */
class MetalTextureResource {
public:
    MetalTextureResource() = default;
    ~MetalTextureResource();

    MetalTextureResource(const MetalTextureResource&) = delete;
    MetalTextureResource& operator=(const MetalTextureResource&) = delete;

    bool create2D(void* device,
                  unsigned long pixelFormat,
                  int width,
                  int height,
                  unsigned long usage,
                  unsigned long storageMode,
                  const QString& label,
                  QString& lastError);
    void destroy();

    bool isValid() const { return texture_ != nullptr; }
    void* handle() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
