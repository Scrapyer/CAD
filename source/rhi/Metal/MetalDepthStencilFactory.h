#pragma once

#include <QString>

struct MetalDepthStencilConfig {
    unsigned long compareFunction = 0;
    bool depthWriteEnabled = false;
    QString label;
};

bool createMetalDepthStencilState(void* device,
                                  const MetalDepthStencilConfig& config,
                                  void*& depthStencilState,
                                  QString& lastError);
