#include "MetalMeshFramePassBuilder.h"

#include "MetalUniformUtils.h"

namespace {

MetalMeshFramePassDraw makeIndexedDraw(bool enabled,
                                       void* pipelineState,
                                       void* vertexBuffer,
                                       void* indexBuffer,
                                       int indexCount,
                                       const MetalMeshUniforms& uniforms)
{
    MetalMeshFramePassDraw draw;
    draw.enabled = enabled;
    draw.pipelineState = pipelineState;
    draw.vertexBuffer = vertexBuffer;
    draw.indexBuffer = indexBuffer;
    draw.indexCount = indexCount;
    draw.uniforms = uniforms;
    return draw;
}

MetalMeshFramePassDraw makeVertexDraw(bool enabled,
                                      void* pipelineState,
                                      void* vertexBuffer,
                                      int vertexCount,
                                      const MetalMeshUniforms& uniforms)
{
    MetalMeshFramePassDraw draw;
    draw.enabled = enabled;
    draw.pipelineState = pipelineState;
    draw.vertexBuffer = vertexBuffer;
    draw.vertexCount = vertexCount;
    draw.uniforms = uniforms;
    return draw;
}

} // namespace

MetalMeshFrameUniformSet buildMetalMeshFrameUniformSet(const QMatrix4x4& mvp,
                                                       const QVector3D& objectColor,
                                                       float scalarMin,
                                                       float scalarMax,
                                                       int numBands,
                                                       bool useVertexScalars,
                                                       const QMatrix4x4& axesMvp)
{
    MetalMeshFrameUniformSet frameUniforms;
    frameUniforms.mesh = makeMetalMeshUniforms(mvp,
                                               objectColor,
                                               1.0f,
                                               scalarMin,
                                               scalarMax,
                                               numBands,
                                               useVertexScalars);
    frameUniforms.line = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.line, 0.08f, 0.10f, 0.12f, 1.0f);
    frameUniforms.overlay = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.overlay, 0.82f, 0.86f, 0.90f, 1.0f);
    frameUniforms.slice = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.slice, 0.15f, 0.78f, 0.95f, 1.0f);
    frameUniforms.isoSurface = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.isoSurface, 0.2f, 0.8f, 0.4f, 0.75f);
    frameUniforms.clipPreview = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.clipPreview, 0.35f, 0.55f, 1.0f, 0.28f);
    frameUniforms.clipPreviewLine = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.clipPreviewLine, 0.68f, 0.78f, 1.0f, 1.0f);
    frameUniforms.selection = frameUniforms.mesh;
    setMetalUniformColor(frameUniforms.selection, 1.0f, 0.76f, 0.18f, 1.0f);
    frameUniforms.axes =
        makeMetalMeshUniforms(axesMvp, QVector3D(0.0f, 0.0f, 0.0f), 1.0f);
    return frameUniforms;
}

MetalMeshFramePassInputs buildMetalMeshFramePassInputs(const MetalMeshFrameDrawFlags& flags,
                                                       const MetalMeshFrameResourceHandles& resources,
                                                       const MetalMeshFrameUniformSet& uniforms)
{
    MetalMeshFramePassInputs framePass;
    framePass.backgroundPipelineState = resources.backgroundPipelineState;
    framePass.depthStencilState = resources.depthStencilState;
    framePass.overlayDepthStencilState = resources.overlayDepthStencilState;
    framePass.drawableSize = resources.drawableSize;
    framePass.backgroundUniforms.bottomColor[0] = resources.backgroundBottomColor.x();
    framePass.backgroundUniforms.bottomColor[1] = resources.backgroundBottomColor.y();
    framePass.backgroundUniforms.bottomColor[2] = resources.backgroundBottomColor.z();
    framePass.backgroundUniforms.bottomColor[3] = 1.0f;
    framePass.backgroundUniforms.topColor[0] = resources.backgroundTopColor.x();
    framePass.backgroundUniforms.topColor[1] = resources.backgroundTopColor.y();
    framePass.backgroundUniforms.topColor[2] = resources.backgroundTopColor.z();
    framePass.backgroundUniforms.topColor[3] = 1.0f;
    framePass.surface = makeIndexedDraw(flags.surface,
                                        resources.meshPipelineState,
                                        resources.meshVertexBuffer,
                                        resources.meshIndexBuffer,
                                        resources.meshIndexCount,
                                        uniforms.mesh);
    framePass.edges = makeIndexedDraw(flags.edges,
                                      resources.linePipelineState,
                                      resources.edgeVertexBuffer,
                                      resources.edgeIndexBuffer,
                                      resources.edgeIndexCount,
                                      uniforms.line);
    framePass.points = makeVertexDraw(flags.points,
                                      resources.linePipelineState,
                                      resources.pointVertexBuffer,
                                      resources.meshVertexCount,
                                      uniforms.line);
    framePass.isoSurface = makeIndexedDraw(flags.isoSurface,
                                           resources.isoSurfacePipelineState,
                                           resources.isoSurfaceVertexBuffer,
                                           resources.isoSurfaceIndexBuffer,
                                           resources.isoSurfaceIndexCount,
                                           uniforms.isoSurface);
    framePass.clipPreview = makeIndexedDraw(flags.clipPreview,
                                            resources.isoSurfacePipelineState,
                                            resources.clipPreviewVertexBuffer,
                                            resources.clipPreviewIndexBuffer,
                                            resources.clipPreviewIndexCount,
                                            uniforms.clipPreview);
    framePass.overlay = makeVertexDraw(flags.overlay,
                                       resources.linePipelineState,
                                       resources.overlayVertexBuffer,
                                       resources.overlayVertexCount,
                                       uniforms.overlay);
    framePass.slice = makeVertexDraw(flags.slice,
                                     resources.linePipelineState,
                                     resources.sliceVertexBuffer,
                                     resources.sliceVertexCount,
                                     uniforms.slice);
    framePass.clipPreviewLines = makeVertexDraw(flags.clipPreviewLines,
                                                resources.linePipelineState,
                                                resources.clipPreviewLineVertexBuffer,
                                                resources.clipPreviewLineVertexCount,
                                                uniforms.clipPreviewLine);
    framePass.selection = makeVertexDraw(flags.selection,
                                         resources.linePipelineState,
                                         resources.selectionVertexBuffer,
                                         resources.selectionVertexCount,
                                         uniforms.selection);
    framePass.axes = makeVertexDraw(flags.axes,
                                    resources.linePipelineState,
                                    resources.axesLineVertexBuffer,
                                    resources.axesLineVertexCount,
                                    uniforms.axes);
    return framePass;
}
