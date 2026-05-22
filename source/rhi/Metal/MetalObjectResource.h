#pragma once

/**
 * @brief Metal/Objective-C 对象资源封装。
 *
 * 用于 device、command queue、CAMetalLayer 这类外层对象；支持接管已有 +1 引用，
 * 或 retain 一个由宿主窗口持有的对象。
 */
class MetalObjectResource {
public:
    MetalObjectResource() = default;
    ~MetalObjectResource();

    MetalObjectResource(const MetalObjectResource&) = delete;
    MetalObjectResource& operator=(const MetalObjectResource&) = delete;

    void adopt(void* object);
    void retain(void* object);
    void destroy();

    bool isValid() const { return object_ != nullptr; }
    void* handle() const { return object_; }

private:
    void* object_ = nullptr;
};
