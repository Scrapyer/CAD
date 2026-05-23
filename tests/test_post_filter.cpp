/**
 * @file test_post_filter.cpp
 * @brief FEPostFilter 和 FEIsoSurface 单元测试
 */

#include "FEPostFilter.h"
#include "FEIsoSurface.h"
#include "FEMeshConverter.h"
#include "FEModel.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>

// 构建含两个 QUAD4 单元的简单 FERenderData（每个单元 2 个三角形）
static FERenderData buildTwoQuadData() {
    FERenderData rd;
    // 4 个顶点组成 2 个三角形（单元1），再 4 个顶点组成 2 个三角形（单元2）
    // 单元 1: 节点 1,2,3,4 → 三角形 (0,1,2) 和 (0,2,3)
    // 单元 2: 节点 3,4,5,6 → 三角形 (4,5,6) 和 (4,6,7)

    float verts[] = {
        // 单元1
        0,0,0, 0,0,1,  // v0 → node 1
        1,0,0, 0,0,1,  // v1 → node 2
        1,1,0, 0,0,1,  // v2 → node 3
        0,1,0, 0,0,1,  // v3 → node 4
        // 单元2
        1,0,0, 0,0,1,  // v4 → node 3
        2,0,0, 0,0,1,  // v5 → node 5
        2,1,0, 0,0,1,  // v6 → node 6
        1,1,0, 0,0,1,  // v7 → node 4
    };
    rd.mesh.vertices.assign(verts, verts + sizeof(verts)/sizeof(float));

    unsigned int idx[] = {0,1,2, 0,2,3, 4,5,6, 4,6,7};
    rd.mesh.indices.assign(idx, idx + 12);

    rd.triangleToElement = {10, 10, 20, 20};
    rd.triangleToFace = {0, 0, 0, 0};
    rd.vertexToNode = {1, 2, 3, 4, 3, 5, 6, 4};
    rd.triangleToPart = {0, 0, 0, 0};

    return rd;
}

static FERenderData buildSingleTriangleData() {
    FERenderData rd;

    float verts[] = {
        0,0,0, 0,0,1,
        1,0,0, 0,0,1,
        1,1,0, 0,0,1,
    };
    rd.mesh.vertices.assign(verts, verts + sizeof(verts)/sizeof(float));

    unsigned int idx[] = {0, 1, 2};
    rd.mesh.indices.assign(idx, idx + 3);

    rd.triangleToElement = {10};
    rd.triangleToFace = {0};
    rd.vertexToNode = {1, 2, 3};

    return rd;
}

static FERenderData buildCrossingTriangleData() {
    FERenderData rd;

    float verts[] = {
        -1,0,0, 0,0,1,
         1,0,0, 0,0,1,
         1,1,0, 0,0,1,
    };
    rd.mesh.vertices.assign(verts, verts + sizeof(verts)/sizeof(float));

    unsigned int idx[] = {0, 1, 2};
    rd.mesh.indices.assign(idx, idx + 3);

    rd.triangleToElement = {10};
    rd.triangleToFace = {0};
    rd.vertexToNode = {1, 2, 3};
    rd.triangleToPart = {0};

    return rd;
}

static FEModel buildTwoBarModel() {
    FEModel model;
    model.name = "TwoBars";
    model.addNode(1, glm::vec3(0, 0, 0));
    model.addNode(2, glm::vec3(1, 0, 0));
    model.addNode(3, glm::vec3(2, 0, 0));

    model.addElement(10, ElementType::BAR2, {1, 2});
    model.addElement(20, ElementType::BAR2, {2, 3});

    FEPart left;
    left.name = "LeftBar";
    left.nodeIds = {1, 2};
    left.elementIds = {10};
    model.parts.push_back(left);

    FEPart right;
    right.name = "RightBar";
    right.nodeIds = {2, 3};
    right.elementIds = {20};
    model.parts.push_back(right);

    return model;
}

static void testThresholdKeepsTargetElement() {
    auto rd = buildTwoQuadData();

    FEScalarField field;
    field.location = FieldLocation::Element;
    field.values[10] = 100.0f;  // 单元 10
    field.values[20] = 500.0f;  // 单元 20

    // 阈值 [400, 600] 只保留单元 20
    auto filtered = FEPostFilter::thresholdByElementValue(rd, field, 400.0f, 600.0f);

    assert(filtered.triangleCount() == 2);
    assert(filtered.triangleToElement[0] == 20);
    assert(filtered.triangleToElement[1] == 20);
    printf("  PASS: threshold keeps target element\n");
}

static void testThresholdPreservesMapping() {
    auto rd = buildTwoQuadData();

    FEScalarField field;
    field.location = FieldLocation::Element;
    field.values[10] = 100.0f;
    field.values[20] = 500.0f;

    auto filtered = FEPostFilter::thresholdByElementValue(rd, field, 0.0f, 200.0f);

    assert(filtered.triangleCount() == 2);
    assert(filtered.triangleToElement[0] == 10);

    // vertexToNode 应该只包含单元 10 的节点
    bool hasNode1 = false, hasNode2 = false;
    for (int nid : filtered.vertexToNode) {
        if (nid == 1) hasNode1 = true;
        if (nid == 2) hasNode2 = true;
    }
    assert(hasNode1 && hasNode2);
    printf("  PASS: threshold preserves mapping\n");
}

static void testBarElementsGenerateEdgeElementMapping() {
    FEModel model = buildTwoBarModel();
    FERenderData rd = FEMeshConverter::toRenderData(model);

    assert(rd.triangleCount() == 0);
    assert(rd.mesh.edgeIndices.size() == 4);
    assert(rd.mesh.edgeToElement.size() == 2);
    assert(rd.mesh.edgeNodeIds.size() == 2);
    assert(rd.edgeToPart.size() == 2);
    assert(rd.mesh.elemEdgeToElement.size() == 2);
    assert(rd.mesh.edgeToElement[0] == 10);
    assert(rd.mesh.edgeToElement[1] == 20);
    assert(rd.edgeToPart[0] == 0);
    assert(rd.edgeToPart[1] == 1);
    assert((rd.mesh.edgeNodeIds[0] == std::pair<int, int>{1, 2}));
    assert((rd.mesh.edgeNodeIds[1] == std::pair<int, int>{2, 3}));
    bool hasElemEdge10 = false;
    bool hasElemEdge20 = false;
    for (int elemId : rd.mesh.elemEdgeToElement) {
        hasElemEdge10 = hasElemEdge10 || elemId == 10;
        hasElemEdge20 = hasElemEdge20 || elemId == 20;
    }
    assert(hasElemEdge10 && hasElemEdge20);

    printf("  PASS: BAR2 conversion generates edge-to-element mapping\n");
}

static void testThresholdFiltersLineOnlyElements() {
    FEModel model = buildTwoBarModel();
    FERenderData rd = FEMeshConverter::toRenderData(model);

    FEScalarField field;
    field.location = FieldLocation::Element;
    field.values[10] = 0.25f;
    field.values[20] = 0.75f;

    FERenderData filtered = FEPostFilter::thresholdByElementValue(rd, field, 0.5f, 1.0f);

    assert(filtered.triangleCount() == 0);
    assert(filtered.mesh.edgeIndices.size() == 2);
    assert(filtered.mesh.edgeToElement.size() == 1);
    assert(filtered.mesh.edgeToElement[0] == 20);
    assert(filtered.mesh.edgeNodeIds.size() == 1);
    assert((filtered.mesh.edgeNodeIds[0] == std::pair<int, int>{2, 3}));
    assert(filtered.edgeToPart.size() == 1);
    assert(filtered.edgeToPart[0] == 1);
    assert(filtered.mesh.elemEdgeToElement.size() == 1);
    assert(filtered.mesh.elemEdgeToElement[0] == 20);

    printf("  PASS: threshold filters line-only BAR2 elements\n");
}

static void testClipFiltersLineOnlyElements() {
    FEModel model = buildTwoBarModel();
    FERenderData rd = FEMeshConverter::toRenderData(model);

    FEPlane plane;
    plane.origin = glm::vec3(1.5f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    FERenderData clipped = FEPostFilter::clipByPlane(rd, plane, true);

    assert(clipped.triangleCount() == 0);
    assert(clipped.mesh.edgeIndices.size() == 2);
    assert(clipped.mesh.edgeToElement.size() == 1);
    assert(clipped.mesh.edgeToElement[0] == 20);
    assert(clipped.mesh.edgeNodeIds.size() == 1);
    assert(clipped.mesh.edgeNodeIds[0].first == -1);
    assert(clipped.mesh.edgeNodeIds[0].second == 3);
    assert(clipped.edgeToPart.size() == 1);
    assert(clipped.edgeToPart[0] == 1);
    assert(clipped.mesh.elemEdgeToElement.size() == 1);
    assert(clipped.mesh.elemEdgeToElement[0] == 20);

    const unsigned int firstVertex = clipped.mesh.edgeIndices[0];
    const unsigned int secondVertex = clipped.mesh.edgeIndices[1];
    const float x0 = clipped.mesh.edgeVertices[firstVertex * 3];
    const float x1 = clipped.mesh.edgeVertices[secondVertex * 3];
    assert(std::abs(x0 - 1.5f) < 1.0e-5f);
    assert(std::abs(x1 - 2.0f) < 1.0e-5f);
    assert(clipped.mesh.elemEdgeVertices.size() == 6);
    assert(std::abs(clipped.mesh.elemEdgeVertices[0] - 1.5f) < 1.0e-5f);
    assert(std::abs(clipped.mesh.elemEdgeVertices[3] - 2.0f) < 1.0e-5f);
    assert(clipped.mesh.elemEdgeNodeIds.size() == 1);
    assert(clipped.mesh.elemEdgeNodeIds[0].first == -1);
    assert(clipped.mesh.elemEdgeNodeIds[0].second == 3);

    printf("  PASS: clip filters line-only BAR2 elements\n");
}

static void testClipByPlane() {
    auto rd = buildTwoQuadData();

    // X=1.0 处的平面，法线朝 +X
    FEPlane plane;
    plane.origin = glm::vec3(1.0f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    // 保留正方向一侧
    // 单元1 质心 ~(0.5, 0.5, 0) → 负侧，被过滤
    // 单元2 质心 ~(1.5, 0.5, 0) → 正侧，保留
    auto filtered = FEPostFilter::clipByPlane(rd, plane, true);

    assert(filtered.triangleCount() == 2);
    assert(filtered.triangleToElement[0] == 20);
    assert(filtered.triangleToElement[1] == 20);
    printf("  PASS: clip by plane filters one side\n");
}

static void testClipNegativeSide() {
    auto rd = buildTwoQuadData();

    FEPlane plane;
    plane.origin = glm::vec3(1.0f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    auto filtered = FEPostFilter::clipByPlane(rd, plane, false);
    assert(filtered.triangleCount() == 2);
    assert(filtered.triangleToElement[0] == 10);
    printf("  PASS: clip keeps negative side\n");
}

static void testClipCutsTriangleAtPlane() {
    auto rd = buildCrossingTriangleData();

    FEPlane plane;
    plane.origin = glm::vec3(0.0f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    auto filtered = FEPostFilter::clipByPlane(rd, plane, true);
    assert(filtered.triangleCount() >= 1);

    for (int vi = 0; vi < filtered.vertexCount(); ++vi) {
        float x = filtered.mesh.vertices[vi * 6];
        assert(x >= -1.0e-5f);
    }

    bool hasCutVertex = false;
    for (int vi = 0; vi < filtered.vertexCount(); ++vi) {
        float x = filtered.mesh.vertices[vi * 6];
        if (std::abs(x) < 1.0e-5f)
            hasCutVertex = true;
    }
    assert(hasCutVertex);
    printf("  PASS: clip cuts triangle at plane\n");
}

static void testSliceByPlane() {
    auto rd = buildTwoQuadData();

    // X=0.5 平面应与单元1 的三角形相交
    FEPlane plane;
    plane.origin = glm::vec3(0.5f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    auto slice = FEPostFilter::sliceByPlane(rd, plane);
    assert(slice.lineCount > 0);
    assert(slice.lineVertices.size() == static_cast<size_t>(slice.lineCount * 6));
    printf("  PASS: slice generates intersection lines (%d lines)\n", slice.lineCount);
}

static void testSliceIgnoresSingleVertexTouch() {
    auto rd = buildSingleTriangleData();

    // X=0 平面只接触三角形的一个顶点，不应生成零长度切片线段
    FEPlane plane;
    plane.origin = glm::vec3(0.0f, 0.0f, 0.0f);
    plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);

    auto slice = FEPostFilter::sliceByPlane(rd, plane);
    assert(slice.lineCount == 0);
    assert(slice.lineVertices.empty());
    printf("  PASS: slice ignores single-vertex touch\n");
}

static void testEmptyInput() {
    FERenderData empty;
    FEScalarField field;
    FEPlane plane;

    auto t = FEPostFilter::thresholdByElementValue(empty, field, 0, 1);
    assert(t.triangleCount() == 0);

    auto c = FEPostFilter::clipByPlane(empty, plane, true);
    assert(c.triangleCount() == 0);

    auto s = FEPostFilter::sliceByPlane(empty, plane);
    assert(s.lineCount == 0);

    printf("  PASS: empty input returns empty output\n");
}

static void testIsoSurfaceTet4() {
    FEModel model;
    // 一个四面体: 节点 1-4
    model.nodes[1] = {1, glm::vec3(0, 0, 0)};
    model.nodes[2] = {2, glm::vec3(1, 0, 0)};
    model.nodes[3] = {3, glm::vec3(0, 1, 0)};
    model.nodes[4] = {4, glm::vec3(0, 0, 1)};

    FEElement tet;
    tet.id = 1;
    tet.type = ElementType::TET4;
    tet.nodeIds = {1, 2, 3, 4};
    model.elements[1] = tet;

    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 0.0f;
    field.values[2] = 1.0f;
    field.values[3] = 1.0f;
    field.values[4] = 1.0f;

    // isoValue=0.5 应该在四面体内部生成一个三角面
    Mesh iso = FEIsoSurface::extract(model, field, 0.5f);
    assert(iso.indices.size() > 0);
    printf("  PASS: iso-surface TET4 (%d triangles)\n",
           static_cast<int>(iso.indices.size() / 3));
}

static void testIsoSurfaceHex8() {
    FEModel model;
    // 一个单位立方体 HEX8
    model.nodes[1] = {1, glm::vec3(0, 0, 0)};
    model.nodes[2] = {2, glm::vec3(1, 0, 0)};
    model.nodes[3] = {3, glm::vec3(1, 1, 0)};
    model.nodes[4] = {4, glm::vec3(0, 1, 0)};
    model.nodes[5] = {5, glm::vec3(0, 0, 1)};
    model.nodes[6] = {6, glm::vec3(1, 0, 1)};
    model.nodes[7] = {7, glm::vec3(1, 1, 1)};
    model.nodes[8] = {8, glm::vec3(0, 1, 1)};

    FEElement hex;
    hex.id = 1;
    hex.type = ElementType::HEX8;
    hex.nodeIds = {1, 2, 3, 4, 5, 6, 7, 8};
    model.elements[1] = hex;

    FEScalarField field;
    field.location = FieldLocation::Node;
    // 底面 0，顶面 1
    field.values[1] = 0.0f; field.values[2] = 0.0f;
    field.values[3] = 0.0f; field.values[4] = 0.0f;
    field.values[5] = 1.0f; field.values[6] = 1.0f;
    field.values[7] = 1.0f; field.values[8] = 1.0f;

    Mesh iso = FEIsoSurface::extract(model, field, 0.5f);
    assert(iso.indices.size() > 0);
    printf("  PASS: iso-surface HEX8 (%d triangles)\n",
           static_cast<int>(iso.indices.size() / 3));
}

static void testIsoSurfaceNoIntersection() {
    FEModel model;
    model.nodes[1] = {1, glm::vec3(0, 0, 0)};
    model.nodes[2] = {2, glm::vec3(1, 0, 0)};
    model.nodes[3] = {3, glm::vec3(0, 1, 0)};
    model.nodes[4] = {4, glm::vec3(0, 0, 1)};

    FEElement tet;
    tet.id = 1;
    tet.type = ElementType::TET4;
    tet.nodeIds = {1, 2, 3, 4};
    model.elements[1] = tet;

    FEScalarField field;
    field.location = FieldLocation::Node;
    field.values[1] = 0.0f; field.values[2] = 0.1f;
    field.values[3] = 0.2f; field.values[4] = 0.3f;

    // isoValue=5.0 超出范围，无交面
    Mesh iso = FEIsoSurface::extract(model, field, 5.0f);
    assert(iso.indices.size() == 0);
    printf("  PASS: iso-surface no intersection\n");
}

int main() {
    printf("=== FEPostFilter Tests ===\n");
    testThresholdKeepsTargetElement();
    testThresholdPreservesMapping();
    testBarElementsGenerateEdgeElementMapping();
    testThresholdFiltersLineOnlyElements();
    testClipFiltersLineOnlyElements();
    testClipByPlane();
    testClipNegativeSide();
    testClipCutsTriangleAtPlane();
    testSliceByPlane();
    testSliceIgnoresSingleVertexTouch();
    testEmptyInput();

    printf("\n=== FEIsoSurface Tests ===\n");
    testIsoSurfaceTet4();
    testIsoSurfaceHex8();
    testIsoSurfaceNoIntersection();

    printf("\nAll post filter tests passed!\n");
    return 0;
}
