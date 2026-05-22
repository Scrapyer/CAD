#pragma once

#include "RenderBackend.h"

#include <QString>

struct MetalDeviceContext {
    void* device = nullptr;
    void* commandQueue = nullptr;
    RenderBackendInfo info;
    QString error;
};

bool isMetalSystemDeviceAvailable();
MetalDeviceContext createMetalDeviceContext();
