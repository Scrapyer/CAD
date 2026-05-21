/**
 * @file FEParser_occ.cpp
 * @brief OpenCASCADE CAD 交换格式解析实现
 */

#include "FEParser.h"

#include <glm/geometric.hpp>

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#ifdef FERENDER_HAS_OPENCASCADE_IMPORT
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#endif

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

void addCadTriangle(FEModel& model,
                    FEPart& part,
                    int& nextNodeId,
                    int& nextElementId,
                    const glm::vec3& a,
                    const glm::vec3& b,
                    const glm::vec3& c)
{
    const glm::vec3 n = glm::cross(b - a, c - a);
    if (glm::dot(n, n) <= 1.0e-20f) {
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

#ifdef FERENDER_HAS_OPENCASCADE_IMPORT

bool readCadShape(const QString& filePath, TopoDS_Shape& shape)
{
    const QByteArray pathBytes = QFile::encodeName(filePath);
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "step" || suffix == "stp") {
        STEPControl_Reader reader;
        if (reader.ReadFile(pathBytes.constData()) != IFSelect_RetDone) {
            return false;
        }
        const Standard_Integer roots = reader.NbRootsForTransfer();
        for (Standard_Integer i = 1; i <= roots; ++i) {
            reader.TransferRoot(i);
        }
        shape = reader.OneShape();
        return !shape.IsNull();
    }

    if (suffix == "iges" || suffix == "igs") {
        IGESControl_Reader reader;
        if (reader.ReadFile(pathBytes.constData()) != IFSelect_RetDone) {
            return false;
        }
        reader.TransferRoots();
        shape = reader.OneShape();
        return !shape.IsNull();
    }

    return false;
}

double meshDeflectionForShape(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (box.IsVoid()) {
        return 0.1;
    }

    Standard_Real xmin = 0.0, ymin = 0.0, zmin = 0.0;
    Standard_Real xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = static_cast<double>(xmax - xmin);
    const double dy = static_cast<double>(ymax - ymin);
    const double dz = static_cast<double>(zmax - zmin);
    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    return std::max(1.0e-4, diagonal * 0.001);
}

bool triangulateCadShape(const TopoDS_Shape& shape,
                         FEModel& model,
                         const std::function<void(int)>& progress)
{
    const double deflection = meshDeflectionForShape(shape);
    BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, 0.5, Standard_True);
    mesher.Perform();
    if (!mesher.IsDone()) {
        return false;
    }

    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        faces.push_back(TopoDS::Face(explorer.Current()));
    }

    FEPart part;
    part.name = "CAD Geometry";
    part.visible = true;

    int nextNodeId = 1;
    int nextElementId = 1;
    const int totalFaces = std::max(1, static_cast<int>(faces.size()));

    for (int fi = 0; fi < static_cast<int>(faces.size()); ++fi) {
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(faces[fi], location);
        if (triangulation.IsNull()) {
            continue;
        }

        const gp_Trsf transform = location.Transformation();
        for (Standard_Integer ti = 1; ti <= triangulation->NbTriangles(); ++ti) {
            Standard_Integer n1 = 0, n2 = 0, n3 = 0;
            triangulation->Triangle(ti).Get(n1, n2, n3);
            if (faces[fi].Orientation() == TopAbs_REVERSED) {
                std::swap(n2, n3);
            }

            const gp_Pnt p1 = triangulation->Node(n1).Transformed(transform);
            const gp_Pnt p2 = triangulation->Node(n2).Transformed(transform);
            const gp_Pnt p3 = triangulation->Node(n3).Transformed(transform);
            addCadTriangle(model, part, nextNodeId, nextElementId,
                           glm::vec3(static_cast<float>(p1.X()), static_cast<float>(p1.Y()), static_cast<float>(p1.Z())),
                           glm::vec3(static_cast<float>(p2.X()), static_cast<float>(p2.Y()), static_cast<float>(p2.Z())),
                           glm::vec3(static_cast<float>(p3.X()), static_cast<float>(p3.Y()), static_cast<float>(p3.Z())));
        }

        if (progress) {
            progress((fi + 1) * 100 / totalFaces);
        }
    }

    if (!part.elementIds.empty()) {
        model.parts.push_back(part);
    }
    return !model.elements.empty();
}

#endif

} // namespace

bool FEParser::parseCadGeometry(const QString& filePath,
                                FEModel& model,
                                const std::function<void(int)>& progress)
{
#ifndef FERENDER_HAS_OPENCASCADE_IMPORT
    Q_UNUSED(filePath);
    Q_UNUSED(model);
    Q_UNUSED(progress);
    qWarning("parseCadGeometry: OpenCASCADE import support is not enabled");
    return false;
#else
    TopoDS_Shape shape;
    if (!readCadShape(filePath, shape)) {
        qWarning("parseCadGeometry: unsupported or invalid CAD file %s", qPrintable(filePath));
        return false;
    }

    model.clear();
    model.name = QFileInfo(filePath).baseName().toStdString();
    model.filePath = filePath.toStdString();

    const bool ok = triangulateCadShape(shape, model, progress);
    qDebug("parseCadGeometry: nodes=%d elements=%d",
           model.nodeCount(),
           model.elementCount());
    return ok;
#endif
}
