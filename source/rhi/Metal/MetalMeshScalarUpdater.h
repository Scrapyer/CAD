#pragma once

#include <QString>

#include <vector>

bool updateMetalMeshVertexScalars(void* vertexBuffer,
                                  int vertexCount,
                                  const std::vector<unsigned int>& scalarSourceIndices,
                                  const std::vector<float>& scalars,
                                  QString& lastError);
