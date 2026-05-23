#pragma once

#include "MetalBufferResource.h"
#include "MetalShaderTypes.h"

#include <QString>

#include <vector>

bool uploadMetalLineVertices(void* device,
                             const std::vector<float>& lineVertices,
                             MetalBufferResource& buffer,
                             int& vertexCount,
                             const QString& label,
                             QString& lastError);
