#include "MetalPickPassBuilder.h"

#include "MetalUniformUtils.h"

#include <QVector3D>

MetalPickPassInputs buildMetalPickPassInputs(const MetalPickPassResourceHandles& resources,
                                             const QMatrix4x4& mvp,
                                             int pickX,
                                             int pickY)
{
    MetalPickPassInputs pickPass;
    pickPass.commandQueue = resources.commandQueue;
    pickPass.colorTexture = resources.colorTexture;
    pickPass.depthTexture = resources.depthTexture;
    pickPass.readbackBuffer = resources.readbackBuffer;
    pickPass.depthStencilState = resources.depthStencilState;
    pickPass.pickPipelineState = resources.pickPipelineState;
    pickPass.meshVertexBuffer = resources.meshVertexBuffer;
    pickPass.meshIndexBuffer = resources.meshIndexBuffer;
    pickPass.meshIndexCount = resources.meshIndexCount;
    pickPass.pickX = pickX;
    pickPass.pickY = pickY;
    pickPass.uniforms = makeMetalMeshUniforms(mvp, QVector3D(1.0f, 1.0f, 1.0f), 1.0f);
    return pickPass;
}
