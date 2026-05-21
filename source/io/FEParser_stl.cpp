/**
 * @file FEParser_stl.cpp
 * @brief STL 三角面几何解析实现
 */

#include "FEParser.h"

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <vector>

namespace {

constexpr float kDegenerateTriangleEps = 1.0e-20f;

bool isDegenerateTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    const glm::vec3 areaNormal = glm::cross(b - a, c - a);
    return glm::dot(areaNormal, areaNormal) <= kDegenerateTriangleEps;
}

void addStlTriangle(FEModel& model,
                    FEPart& part,
                    int& nextNodeId,
                    int& nextElementId,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec3& c)
{
    if (isDegenerateTriangle(a, b, c)) {
        return;
    }

    const int n0 = nextNodeId++;
    const int n1 = nextNodeId++;
    const int n2 = nextNodeId++;
    const int eid = nextElementId++;

    model.addNode(n0, a);
    model.addNode(n1, b);
    model.addNode(n2, c);
    model.addElement(eid, ElementType::TRI3, {n0, n1, n2});
    part.nodeIds.push_back(n0);
    part.nodeIds.push_back(n1);
    part.nodeIds.push_back(n2);
    part.elementIds.push_back(eid);
}

bool looksLikeBinaryStl(QFile& file, quint32& triangleCount)
{
    if (file.size() < 84) {
        return false;
    }

    if (!file.seek(80)) {
        return false;
    }

    char countBytes[4] = {};
    if (file.read(countBytes, 4) != 4) {
        return false;
    }

    triangleCount = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(countBytes));
    const quint64 expectedSize = 84ull + static_cast<quint64>(triangleCount) * 50ull;
    return expectedSize == static_cast<quint64>(file.size());
}

bool parseBinaryStl(QFile& file,
                    FEModel& model,
                    const std::function<void(int)>& progress,
                    quint32 triangleCount)
{
    if (!file.seek(84)) {
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

    FEPart part;
    part.name = "STL Geometry";
    part.visible = true;

    int nextNodeId = 1;
    int nextElementId = 1;
    const quint32 reportInterval = std::max<quint32>(1, triangleCount / 100);

    for (quint32 i = 0; i < triangleCount; ++i) {
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        quint16 attr = 0;

        stream >> nx >> ny >> nz;
        stream >> ax >> ay >> az;
        stream >> bx >> by >> bz;
        stream >> cx >> cy >> cz;
        stream >> attr;

        if (stream.status() != QDataStream::Ok) {
            qWarning("parseStlGeometry: truncated binary STL %s", qPrintable(file.fileName()));
            return false;
        }

        addStlTriangle(model, part, nextNodeId, nextElementId,
                       glm::vec3(ax, ay, az),
                       glm::vec3(bx, by, bz),
                       glm::vec3(cx, cy, cz));

        if (progress && (i % reportInterval == 0)) {
            progress(static_cast<int>(i * 100 / std::max<quint32>(1, triangleCount)));
        }
    }

    if (!part.elementIds.empty()) {
        model.parts.push_back(part);
    }
    if (progress) progress(100);
    return !model.elements.empty();
}

bool parseAsciiStl(QFile& file, FEModel& model, const std::function<void(int)>& progress)
{
    if (!file.seek(0)) {
        return false;
    }

    QTextStream stream(&file);
    FEPart part;
    part.name = "STL Geometry";
    part.visible = true;

    int nextNodeId = 1;
    int nextElementId = 1;
    std::vector<glm::vec3> vertices;
    vertices.reserve(3);

    const qint64 totalSize = std::max<qint64>(1, file.size());
    int lastProgress = -1;

    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith("vertex", Qt::CaseInsensitive)) {
            const QStringList tokens = line.simplified().split(' ');
            if (tokens.size() >= 4) {
                vertices.emplace_back(tokens[1].toFloat(), tokens[2].toFloat(), tokens[3].toFloat());
                if (vertices.size() == 3) {
                    addStlTriangle(model, part, nextNodeId, nextElementId,
                                   vertices[0], vertices[1], vertices[2]);
                    vertices.clear();
                }
            }
        }

        if (progress) {
            const int pct = static_cast<int>(file.pos() * 100 / totalSize);
            if (pct != lastProgress) {
                progress(pct);
                lastProgress = pct;
            }
        }
    }

    if (!part.elementIds.empty()) {
        model.parts.push_back(part);
    }
    if (progress) progress(100);
    return !model.elements.empty();
}

} // namespace

bool FEParser::parseStlGeometry(const QString& filePath,
                                FEModel& model,
                                const std::function<void(int)>& progress)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("parseStlGeometry: cannot open %s", qPrintable(filePath));
        return false;
    }

    model.clear();
    model.name = QFileInfo(filePath).baseName().toStdString();
    model.filePath = filePath.toStdString();

    quint32 triangleCount = 0;
    const bool binary = looksLikeBinaryStl(file, triangleCount);
    const bool ok = binary
        ? parseBinaryStl(file, model, progress, triangleCount)
        : parseAsciiStl(file, model, progress);

    qDebug("parseStlGeometry: %s STL nodes=%d elements=%d",
           binary ? "binary" : "ascii",
           model.nodeCount(),
           model.elementCount());
    return ok;
}
