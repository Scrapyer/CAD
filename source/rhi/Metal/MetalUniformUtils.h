#pragma once

#include "MetalShaderTypes.h"

#include <QMatrix4x4>
#include <QVector3D>

void setMetalUniformColor(MetalMeshUniforms& uniforms,
                          float red,
                          float green,
                          float blue,
                          float alpha);

MetalMeshUniforms makeMetalMeshUniforms(const QMatrix4x4& mvp,
                                        const QVector3D& color,
                                        float alpha,
                                        float scalarMin = 0.0f,
                                        float scalarMax = 1.0f,
                                        int numBands = 10,
                                        bool useVertexScalars = false);
