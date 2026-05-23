#pragma once

#include "Geometry.h"
#include "MetalShaderTypes.h"

#include <QVector3D>

#include <unordered_map>
#include <unordered_set>
#include <vector>

struct MetalMeshUploadOptions {
    QVector3D objectColor{0.48f, 0.72f, 0.76f};
    std::vector<int> triangleToElement;
    std::vector<int> triangleToPart;
    std::vector<int> edgeToPart;
    std::vector<QVector3D> partColors;
    std::unordered_map<int, bool> partVisibility;
    std::unordered_set<int> hiddenElementIds;
    bool useVertexColor = false;
    std::vector<float> vertexColors;
    std::vector<float> vertexScalars;
    std::vector<float> edgeScalars;
    float scalarMin = 0.0f;
    float scalarMax = 1.0f;
    int numBands = 10;
};

struct MetalMeshUploadData {
    std::vector<MetalMeshVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<MetalLineVertex> pointVertices;
    std::vector<MetalLineVertex> edgeVertices;
    std::vector<unsigned int> edgeIndices;
    std::vector<unsigned int> scalarSourceIndices;
    int edgeVertexCount = 0;
};

MetalMeshUploadData buildMetalMeshUploadData(const Mesh& mesh,
                                             const MetalMeshUploadOptions& options);
