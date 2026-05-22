#include "MetalUniformUtils.h"

#include <algorithm>

void setMetalUniformColor(MetalMeshUniforms& uniforms,
                          float red,
                          float green,
                          float blue,
                          float alpha)
{
    uniforms.color[0] = red;
    uniforms.color[1] = green;
    uniforms.color[2] = blue;
    uniforms.color[3] = alpha;
}

MetalMeshUniforms makeMetalMeshUniforms(const QMatrix4x4& mvp,
                                        const QVector3D& color,
                                        float alpha,
                                        float scalarMin,
                                        float scalarMax,
                                        int numBands,
                                        bool useVertexScalars)
{
    MetalMeshUniforms uniforms{};
    const float* matrixData = mvp.constData();
    std::copy(matrixData, matrixData + 16, uniforms.mvp);
    setMetalUniformColor(uniforms, color.x(), color.y(), color.z(), alpha);
    uniforms.contour[0] = scalarMin;
    uniforms.contour[1] = scalarMax;
    uniforms.contour[2] = static_cast<float>(numBands);
    uniforms.contour[3] = useVertexScalars ? 1.0f : 0.0f;
    return uniforms;
}
