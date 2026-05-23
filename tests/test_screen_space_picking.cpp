#include "ScreenSpacePicking.h"

#include <QPointF>

#include <cassert>
#include <cstdio>
#include <glm/glm.hpp>

static Mesh makeLineMesh()
{
    Mesh mesh;
    mesh.edgeVertices = {
        -0.8f, 0.0f, 0.0f,
         0.0f, 0.0f, 0.0f,
         0.8f, 0.0f, 0.0f,
    };
    mesh.edgeIndices = {0, 1, 1, 2};
    mesh.edgeToElement = {10, 20};
    mesh.edgeNodeIds = {{1, 2}, {2, 3}};
    mesh.elemEdgeVertices = {
        -0.8f, 0.0f, 0.0f,
         0.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 0.0f,
         0.8f, 0.0f, 0.0f,
    };
    mesh.elemEdgeToElement = {10, 20};
    mesh.elemEdgeNodeIds = {{1, 2}, {2, 3}};
    return mesh;
}

int main()
{
    const Mesh mesh = makeLineMesh();
    const glm::mat4 mvp(1.0f);
    const float width = 100.0f;
    const float height = 100.0f;

    auto allVisible = [](int) { return true; };
    const int leftHit = ScreenSpacePicking::edgeElementAtPoint(mesh,
                                                               QPointF(30.0, 52.0),
                                                               mvp,
                                                               width,
                                                               height,
                                                               true,
                                                               5.0f,
                                                               allVisible);
    assert(leftHit == 10);

    auto hideLeft = [](int elementId) { return elementId != 10; };
    const int hiddenLeftHit = ScreenSpacePicking::edgeElementAtPoint(mesh,
                                                                     QPointF(30.0, 52.0),
                                                                     mvp,
                                                                     width,
                                                                     height,
                                                                     true,
                                                                     5.0f,
                                                                     hideLeft);
    assert(hiddenLeftHit == -1);

    const int rightHit = ScreenSpacePicking::edgeElementAtPoint(mesh,
                                                                QPointF(70.0, 48.0),
                                                                mvp,
                                                                width,
                                                                height,
                                                                true,
                                                                5.0f,
                                                                allVisible);
    assert(rightHit == 20);

    const int nearRightNode = ScreenSpacePicking::closestNodeForElement(mesh,
                                                                        {},
                                                                        {},
                                                                        20,
                                                                        QPointF(91.0, 50.0),
                                                                        mvp,
                                                                        width,
                                                                        height,
                                                                        true);
    assert(nearRightNode == 3);

    QPointF openGlScreen;
    QPointF vulkanScreen;
    const bool openGlProjected = ScreenSpacePicking::projectToScreen(glm::vec3(0.0f, 0.5f, 0.0f),
                                                                     mvp,
                                                                     width,
                                                                     height,
                                                                     true,
                                                                     openGlScreen);
    const bool vulkanProjected = ScreenSpacePicking::projectToScreen(glm::vec3(0.0f, 0.5f, 0.0f),
                                                                     mvp,
                                                                     width,
                                                                     height,
                                                                     false,
                                                                     vulkanScreen);
    assert(openGlProjected);
    assert(vulkanProjected);
    assert(openGlScreen.y() == 25.0);
    assert(vulkanScreen.y() == 75.0);

    printf("Screen-space picking helper test passed.\\n");
    return 0;
}
