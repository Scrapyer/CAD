#pragma once

#include "MetalMeshFramePass.h"

#include <QString>

bool submitMetalDrawableFrame(void* metalLayer,
                              void* commandQueue,
                              void* depthTexture,
                              float red,
                              float green,
                              float blue,
                              float alpha,
                              const MetalMeshFramePassInputs& framePass,
                              QString& lastError);
