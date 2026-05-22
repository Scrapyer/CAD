#pragma once

/**
 * @brief Metal pipeline/depth-stencil state 资源封装。
 *
 * 负责接管 Objective-C state object 的生命周期，具体创建逻辑保留在后端中。
 */
class MetalStateResource {
public:
    MetalStateResource() = default;
    ~MetalStateResource();

    MetalStateResource(const MetalStateResource&) = delete;
    MetalStateResource& operator=(const MetalStateResource&) = delete;

    void adopt(void* state);
    void destroy();

    bool isValid() const { return state_ != nullptr; }
    void* handle() const { return state_; }

private:
    void* state_ = nullptr;
};
