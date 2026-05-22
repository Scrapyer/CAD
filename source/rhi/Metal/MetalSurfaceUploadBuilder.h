#pragma once

#include "Geometry.h"
#include "MetalShaderTypes.h"

#include <QVector3D>

#include <vector>

struct MetalSurfaceUploadData {
    std::vector<MetalMeshVertex> vertices;
    std::vector<unsigned int> indices;
};

MetalSurfaceUploadData buildMetalSurfaceUploadData(const Mesh& mesh, const QVector3D& color);
