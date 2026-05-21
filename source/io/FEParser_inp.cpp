/**
 * @file FEParser_inp.cpp
 * @brief ABAQUS INP 文件解析实现
 */

#include "FEParser.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <glm/glm.hpp>

#include <map>
#include <vector>

bool FEParser::parseAbaqusInp(const QString& filePath, FEModel& model, const std::function<void(int)>& progress) {
    // ── 工具 lambda ──

    // ABAQUS 单元类型名 → ElementType
    auto mapElemType = [](const QString& abaqusType) -> ElementType {
        QString t = abaqusType.toUpper().trimmed();
        if (t.contains("C3D20"))  return ElementType::HEX20;
        if (t.contains("C3D8"))   return ElementType::HEX8;
        if (t.contains("C3D10"))  return ElementType::TET10;
        if (t.contains("C3D4"))   return ElementType::TET4;
        if (t.contains("C3D6"))   return ElementType::WEDGE6;
        if (t.contains("C3D5"))   return ElementType::PYRAMID5;
        if (t.contains("S3") || t.contains("CPS3") || t.contains("CPE3"))
            return ElementType::TRI3;
        if (t.contains("STRI65") || t.contains("CPS6") || t.contains("CPE6"))
            return ElementType::TRI6;
        if (t.contains("S8") || t.contains("CPS8") || t.contains("CPE8"))
            return ElementType::QUAD8;
        if (t.contains("S4") || t.contains("CPS4") || t.contains("CPE4"))
            return ElementType::QUAD4;
        if (t.contains("B31") || t.contains("T2D2") || t.contains("B21"))
            return ElementType::BAR2;
        return ElementType::HEX8;
    };

    auto nodeCountForType = [](ElementType t) -> int {
        switch (t) {
            case ElementType::BAR2:     return 2;
            case ElementType::BAR3:     return 3;
            case ElementType::TRI3:     return 3;
            case ElementType::TRI6:     return 6;
            case ElementType::QUAD4:    return 4;
            case ElementType::QUAD8:    return 8;
            case ElementType::TET4:     return 4;
            case ElementType::TET10:    return 10;
            case ElementType::HEX8:     return 8;
            case ElementType::HEX20:    return 20;
            case ElementType::WEDGE6:   return 6;
            case ElementType::PYRAMID5: return 5;
        }
        return 0;
    };

    // 将逗号和 tab 统一分割为 tokens
    auto splitLine = [](const QString& line) -> QStringList {
        QString s = line;
        s.replace('\t', ',');
        QStringList parts = s.split(',');
        QStringList result;
        for (auto& p : parts) {
            QString t = p.trimmed();
            if (!t.isEmpty()) result.append(t);
        }
        return result;
    };

    // ── 解析 INCLUDE 路径：尝试在主文件目录下查找 ──
    QDir baseDir = QFileInfo(filePath).absoluteDir();

    auto resolveInclude = [&](const QString& keyword) -> QString {
        // 提取 INPUT="..." 中的路径
        static const QRegularExpression rx(
            "INPUT\\s*=\\s*\"([^\"]*)\"",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = rx.match(keyword);
        if (!match.hasMatch()) return QString();

        QString rawPath = match.captured(1);
        // 提取文件名（去掉 Windows 路径前缀）
        QString fileName = rawPath;
        int lastSlash = rawPath.lastIndexOf('/');
        int lastBackslash = rawPath.lastIndexOf('\\');
        int sep = qMax(lastSlash, lastBackslash);
        if (sep >= 0) fileName = rawPath.mid(sep + 1);

        // 尝试多个可能的位置
        QStringList candidates;
        candidates << baseDir.filePath(fileName);
        candidates << baseDir.filePath(rawPath);
        // 尝试在子目录中查找（从原始路径提取子目录）
        if (sep >= 0) {
            // 取最后一级子目录 + 文件名
            QString parent = rawPath.left(sep);
            int prevSep = qMax(parent.lastIndexOf('/'), parent.lastIndexOf('\\'));
            if (prevSep >= 0) {
                QString subDir = parent.mid(prevSep + 1);
                candidates << baseDir.filePath(subDir + "/" + fileName);
            }
        }

        for (const auto& c : candidates) {
            if (QFile::exists(c)) return c;
        }
        return QString();
    };

    // ── 递归读取文件，展开所有 INCLUDE 为行列表（0-40%）──
    const qint64 mainFileSize = std::max((qint64)1, QFileInfo(filePath).size());
    qint64 bytesRead = 0;

    std::function<QStringList(const QString&)> readWithIncludes;
    readWithIncludes = [&](const QString& path) -> QStringList {
        QFile f(path);
        QStringList lines;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return lines;

        QTextStream stream(&f);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            bytesRead += line.size() + 1;
            if (progress) {
                int pct = static_cast<int>(std::min(bytesRead * 40 / mainFileSize, (qint64)40));
                progress(pct);
            }

            QString trimmed = line.trimmed();

            // 处理 *INCLUDE
            if (trimmed.toUpper().startsWith("*INCLUDE")) {
                QString incPath = resolveInclude(trimmed);
                if (!incPath.isEmpty()) {
                    lines.append(readWithIncludes(incPath));
                }
            } else {
                lines.append(line);
            }
        }
        return lines;
    };

    // ── 展开所有行 ──
    QStringList allLines = readWithIncludes(filePath);
    if (progress) progress(40);

    // ── 解析（40-100%）──
    enum Section { None, Node, Element } section = None;
    ElementType currentElemType = ElementType::HEX8;
    int expectedNodeCount = 8;
    int pendingElemId = -1;
    std::vector<int> pendingNodeIds;
    QString currentElset;                               // 当前 *Element 关键字上的 ELSET= 值
    std::map<QString, int> elsetToPartIndex;            // ELSET 名 → model.parts 索引

    auto flushPendingElement = [&]() {
        if (pendingElemId >= 0 && !pendingNodeIds.empty()) {
            model.addElement(pendingElemId, currentElemType, pendingNodeIds);
            // 如果该 *Element 段指定了 ELSET，将单元 ID 归入对应部件
            if (!currentElset.isEmpty()) {
                auto it = elsetToPartIndex.find(currentElset);
                if (it == elsetToPartIndex.end()) {
                    // 新 ELSET：创建对应部件
                    FEPart part;
                    part.name = currentElset.toStdString();
                    model.parts.push_back(part);
                    elsetToPartIndex[currentElset] = static_cast<int>(model.parts.size()) - 1;
                    it = elsetToPartIndex.find(currentElset);
                }
                model.parts[it->second].elementIds.push_back(pendingElemId);
            }
            pendingElemId = -1;
            pendingNodeIds.clear();
        }
    };

    const int totalLines = allLines.size();
    const int lineReportInterval = std::max(1, totalLines / 100);
    int lineIndex = 0;

    for (const auto& rawLine : allLines) {
        if (progress && (++lineIndex % lineReportInterval == 0)) {
            progress(40 + lineIndex * 60 / totalLines);
        }
        QString line = rawLine.trimmed();

        if (line.isEmpty()) continue;
        if (line.startsWith("**")) continue;

        // 关键字行
        if (line.startsWith("*")) {
            flushPendingElement();
            QString upper = line.toUpper();

            if (upper.startsWith("*NODE") && !upper.startsWith("*NSET")
                && !upper.contains("OUTPUT")) {
                section = Node;
            } else if (upper.startsWith("*ELEMENT") && upper.contains("TYPE")
                       && !upper.contains("OUTPUT")) {
                section = Element;
                static const QRegularExpression rx(
                    "TYPE\\s*=\\s*([A-Za-z0-9]+)",
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch typeMatch = rx.match(line);
                if (typeMatch.hasMatch()) {
                    currentElemType = mapElemType(typeMatch.captured(1));
                }
                expectedNodeCount = nodeCountForType(currentElemType);
                // 提取可选的 ELSET= 参数（用于部件归属）
                static const QRegularExpression rxElset(
                    "ELSET\\s*=\\s*([A-Za-z0-9_\\-\\.]+)",
                    QRegularExpression::CaseInsensitiveOption);
                const QRegularExpressionMatch elsetMatch = rxElset.match(line);
                currentElset = elsetMatch.hasMatch()
                    ? elsetMatch.captured(1).trimmed()
                    : QString();
            } else {
                section = None;
            }
            continue;
        }

        // 数据行
        if (section == Node) {
            QStringList parts = splitLine(line);
            if (parts.size() >= 4) {
                int id = parts[0].toInt();
                float x = parts[1].toFloat();
                float y = parts[2].toFloat();
                float z = parts[3].toFloat();
                model.addNode(id, glm::vec3(x, y, z));
            }
        } else if (section == Element) {
            QStringList parts = splitLine(line);
            if (parts.isEmpty()) continue;

            if (pendingElemId < 0) {
                // 新单元行：第一个值是单元 ID，后面是节点 ID
                pendingElemId = parts[0].toInt();
                for (int i = 1; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            } else {
                // 续行
                for (int i = 0; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            }

            if (static_cast<int>(pendingNodeIds.size()) >= expectedNodeCount)
                flushPendingElement();
        }
    }

    flushPendingElement();

    qDebug("parseAbaqusInp: nodes=%d, elements=%d",
           (int)model.nodes.size(),
           (int)model.elements.size());

    // 找出缺失的单元 ID
    if ((int)model.elements.size() < 800) {
        QString missing;
        for (int i = 1; i <= 800; ++i) {
            if (model.elements.find(i) == model.elements.end()) {
                if (!missing.isEmpty()) missing += ", ";
                missing += QString::number(i);
            }
        }
        qDebug("  missing element IDs: %s", qPrintable(missing));
    }

    // 检查单元类型分布
    int hex8 = 0, wedge6 = 0, other = 0;
    for (const auto& [id, elem] : model.elements) {
        if (elem.type == ElementType::HEX8) hex8++;
        else if (elem.type == ElementType::WEDGE6) wedge6++;
        else other++;
    }
    qDebug("  HEX8=%d, WEDGE6=%d, other=%d", hex8, wedge6, other);

    return true;
}
