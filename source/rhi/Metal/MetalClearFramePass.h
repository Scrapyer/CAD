#pragma once

#include <QString>

bool executeMetalClearFramePass(void* metalLayer,
                                void* commandQueue,
                                float red,
                                float green,
                                float blue,
                                float alpha,
                                QString& lastError);
