#include "FEParser.h"
#include "FEMeshConverter.h"

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

bool writeAsciiStl(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(
        "solid sample\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid sample\n");
    return true;
}

bool writeBinaryStl(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QByteArray header(80, '\0');
    header.replace(0, 13, "binary sample");
    file.write(header);

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

    stream << quint32(1);
    stream << 0.0f << 0.0f << 1.0f;
    stream << 0.0f << 0.0f << 0.0f;
    stream << 1.0f << 0.0f << 0.0f;
    stream << 0.0f << 1.0f << 0.0f;
    stream << quint16(0);
    return stream.status() == QDataStream::Ok;
}

bool verifyParsedTriangle(const QString& path)
{
    FEModel model;
    if (!expect(FEParser::parseStlGeometry(path, model), "STL should parse")) return false;
    if (!expect(model.nodeCount() == 3, "STL should create three nodes")) return false;
    if (!expect(model.elementCount() == 1, "STL should create one TRI3 element")) return false;
    if (!expect(model.parts.size() == 1, "STL should create one geometry part")) return false;
    if (!expect(model.parts[0].elementIds.size() == 1, "STL part should contain one element")) return false;

    FERenderData rd = FEMeshConverter::toRenderData(model);
    if (!expect(rd.vertexCount() == 3, "STL render data should contain three vertices")) return false;
    if (!expect(rd.triangleCount() == 1, "STL render data should contain one triangle")) return false;
    if (!expect(rd.triangleToPart.size() == 1 && rd.triangleToPart[0] == 0,
                "STL triangle should map to the geometry part")) return false;
    return true;
}

} // namespace

int main()
{
    QTemporaryDir dir;
    if (!expect(dir.isValid(), "temporary directory should be valid")) return 1;

    const QString asciiPath = dir.filePath("ascii.stl");
    const QString binaryPath = dir.filePath("binary.stl");

    if (!expect(writeAsciiStl(asciiPath), "ASCII STL fixture should be written")) return 1;
    if (!expect(writeBinaryStl(binaryPath), "binary STL fixture should be written")) return 1;
    if (!verifyParsedTriangle(asciiPath)) return 1;
    if (!verifyParsedTriangle(binaryPath)) return 1;

    return 0;
}
