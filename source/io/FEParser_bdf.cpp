/**
 * @file FEParser_bdf.cpp
 * @brief Nastran BDF/FEM 文件解析实现
 */

#include "FEParser.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <glm/glm.hpp>

#include <set>
#include <unordered_set>
#include <unordered_map>

bool FEParser::parseNastranBdf(const QString& filePath, FEModel& model, const std::function<void(int)>& progress) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // 读取全部行（0-15%）
    const qint64 fileSize = std::max((qint64)1, QFileInfo(filePath).size());
    qint64 bytesRead = 0;
    QStringList allLines;
    {
        QTextStream stream(&f);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            bytesRead += line.size() + 1;
            if (progress) {
                int pct = static_cast<int>(std::min(bytesRead * 15 / fileSize, (qint64)15));
                progress(pct);
            }
            allLines.append(line);
        }
    }
    f.close();
    if (progress) progress(15);

    const int totalLines = allLines.size();
    const int lineReportInterval = std::max(1, totalLines / 100);

    // ── Nastran 字段解析工具 ──

    // 解析 Nastran 实数（支持隐式指数：如 "1.5-3" → "1.5e-3"）
    auto parseNastranReal = [](const QString& s) -> double {
        QString t = s.trimmed();
        if (t.isEmpty()) return 0.0;
        for (int i = 1; i < t.size(); ++i) {
            QChar c = t[i];
            if ((c == '+' || c == '-') && t[i-1] != 'e' && t[i-1] != 'E'
                && t[i-1] != 'd' && t[i-1] != 'D'
                && t[i-1] != '+' && t[i-1] != '-') {
                if (t[i-1].isDigit() || t[i-1] == '.') {
                    t.insert(i, 'e');
                    break;
                }
            }
        }
        t.replace(QChar('d'), QChar('e'), Qt::CaseInsensitive);
        return t.toDouble();
    };

    // 将一行拆分为字段（自动检测自由格式/小字段/大字段）
    auto splitBdfFields = [](const QString& line) -> QStringList {
        QStringList fields;
        if (line.contains(',')) {
            for (const auto& f : line.split(','))
                fields.append(f.trimmed());
        } else {
            QString first = line.left(8).trimmed();
            fields.append(first);
            bool largeField = first.endsWith('*');
            int fieldWidth = largeField ? 16 : 8;
            int pos = 8;
            while (pos < line.size()) {
                int end = qMin(pos + fieldWidth, line.size());
                fields.append(line.mid(pos, end - pos).trimmed());
                pos = end;
            }
        }
        return fields;
    };

    // ── 合并续行（15-25%）──
    QStringList mergedLines;
    {
        QString current;
        bool currentIsFreeFormat = false;
        const int mergeTotal = allLines.size();
        const int mergeReportInterval = std::max(1, mergeTotal / 10);
        int mergeIndex = 0;

        for (const auto& rawLine : allLines) {
            if (progress && (++mergeIndex % mergeReportInterval == 0)) {
                progress(15 + mergeIndex * 10 / mergeTotal);
            }
            QString line = rawLine;
            int dollarPos = line.indexOf('$');
            if (dollarPos >= 0) line = line.left(dollarPos);
            if (line.trimmed().isEmpty()) continue;

            QChar firstChar = line.trimmed().at(0);
            bool isContinuation = (firstChar == '+' || firstChar == '*')
                                  && !current.isEmpty();

            if (isContinuation) {
                if (currentIsFreeFormat) {
                    QString continuation = line.trimmed();
                    if (continuation.startsWith('+') || continuation.startsWith('*')) {
                        int commaPos = continuation.indexOf(',');
                        if (commaPos >= 0) continuation = continuation.mid(commaPos + 1);
                        else continuation = continuation.mid(8);
                    }
                    current += "," + continuation;
                } else {
                    int dataEnd = qMin(72, line.size());
                    QString contData = (dataEnd > 8) ? line.mid(8, dataEnd - 8) : "";
                    current += contData;
                }
            } else {
                if (!current.isEmpty()) mergedLines.append(current);
                currentIsFreeFormat = line.contains(',');
                if (!currentIsFreeFormat) {
                    current = line.left(qMin(72, line.size()));
                } else {
                    current = line;
                }
            }
        }
        if (!current.isEmpty()) mergedLines.append(current);
    }
    if (progress) progress(25);

    // ── CORD2R 坐标系数据结构 ──
    struct CoordSys {
        int id = 0;
        int rid = 0;
        glm::dvec3 A{0};
        glm::dvec3 B{0};
        glm::dvec3 C{0};
        glm::dmat3 R{1};
        glm::dvec3 origin{0};
        bool resolved = false;
    };
    std::unordered_map<int, CoordSys> coordSystems;

    struct RawGrid {
        int id;
        int cp;
        glm::dvec3 xyz;
    };
    std::vector<RawGrid> rawGrids;

    std::unordered_map<int, int> elemPid;
    std::set<int> propertyIds;

    // ── 第一遍：解析 CORD2R 和 GRID，收集单元（25-80%）──
    int lineIndex = 0;
    const int mergedTotal2 = mergedLines.size();
    const int mergedReportInterval = std::max(1, mergedTotal2 / 100);

    for (const auto& line : mergedLines) {
        if (progress && (++lineIndex % mergedReportInterval == 0)) {
            progress(25 + lineIndex * 55 / mergedTotal2);
        }

        QStringList fields = splitBdfFields(line);
        if (fields.isEmpty()) continue;

        QString card = fields[0].toUpper().remove('*');

        if (card == "CORD2R") {
            if (fields.size() < 6) continue;
            CoordSys cs;
            cs.id  = fields[1].toInt();
            cs.rid = fields[2].toInt();
            cs.A.x = parseNastranReal(fields[3]);
            cs.A.y = parseNastranReal(fields[4]);
            cs.A.z = (fields.size() > 5)  ? parseNastranReal(fields[5])  : 0.0;
            cs.B.x = (fields.size() > 6)  ? parseNastranReal(fields[6])  : 0.0;
            cs.B.y = (fields.size() > 7)  ? parseNastranReal(fields[7])  : 0.0;
            cs.B.z = (fields.size() > 8)  ? parseNastranReal(fields[8])  : 0.0;
            cs.C.x = (fields.size() > 9)  ? parseNastranReal(fields[9])  : 0.0;
            cs.C.y = (fields.size() > 10) ? parseNastranReal(fields[10]) : 0.0;
            cs.C.z = (fields.size() > 11) ? parseNastranReal(fields[11]) : 0.0;
            coordSystems[cs.id] = cs;
        }
        else if (card == "GRID") {
            if (fields.size() < 5) continue;
            RawGrid g;
            g.id = fields[1].toInt();
            g.cp = fields[2].toInt();
            g.xyz.x = parseNastranReal(fields[3]);
            g.xyz.y = parseNastranReal(fields[4]);
            g.xyz.z = (fields.size() > 5) ? parseNastranReal(fields[5]) : 0.0;
            rawGrids.push_back(g);
        }
        else if (card == "CTETRA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 10)
                model.addElement(eid, ElementType::TET10, std::vector<int>(nids.begin(), nids.begin() + 10));
            else if (nids.size() >= 4)
                model.addElement(eid, ElementType::TET4, std::vector<int>(nids.begin(), nids.begin() + 4));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CHEXA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 20)
                model.addElement(eid, ElementType::HEX20, std::vector<int>(nids.begin(), nids.begin() + 20));
            else if (nids.size() >= 8)
                model.addElement(eid, ElementType::HEX8, std::vector<int>(nids.begin(), nids.begin() + 8));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CPENTA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 6)
                model.addElement(eid, ElementType::WEDGE6, std::vector<int>(nids.begin(), nids.begin() + 6));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CPYRAM") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 5)
                model.addElement(eid, ElementType::PYRAMID5, std::vector<int>(nids.begin(), nids.begin() + 5));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CTRIA3") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::TRI3,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CTRIA6") {
            if (fields.size() < 9) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 9 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::TRI6, nids);
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CQUAD4") {
            if (fields.size() < 7) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::QUAD4,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt(), fields[6].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CQUAD8") {
            if (fields.size() < 11) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 11 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::QUAD8, nids);
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CBAR" || card == "CBEAM") {
            if (fields.size() < 5) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::BAR2,
                {fields[3].toInt(), fields[4].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
    }

    // ── 解析 CORD2R 坐标系变换矩阵（支持链式引用） ──
    std::function<void(CoordSys&)> resolveCoordSys;
    resolveCoordSys = [&](CoordSys& cs) {
        if (cs.resolved) return;
        cs.resolved = true;

        if (cs.rid != 0) {
            auto it = coordSystems.find(cs.rid);
            if (it != coordSystems.end()) {
                resolveCoordSys(it->second);
                CoordSys& ref = it->second;
                cs.A = ref.origin + ref.R * cs.A;
                cs.B = ref.origin + ref.R * cs.B;
                cs.C = ref.origin + ref.R * cs.C;
            }
        }

        glm::dvec3 zDir = glm::normalize(cs.B - cs.A);
        glm::dvec3 xzVec = cs.C - cs.A;
        glm::dvec3 yDir = glm::normalize(glm::cross(zDir, xzVec));
        glm::dvec3 xDir = glm::cross(yDir, zDir);

        cs.R = glm::dmat3(xDir, yDir, zDir);
        cs.origin = cs.A;
    };

    for (auto& [id, cs] : coordSystems) {
        resolveCoordSys(cs);
    }

    qDebug("parseNastranBdf: %d coordinate systems resolved", (int)coordSystems.size());

    // ── 将 GRID 节点从局部坐标系变换到全局坐标系 ──（80-95%）
    int gridIndex = 0;
    const int gridTotal = static_cast<int>(rawGrids.size());
    const int gridReportInterval = std::max(1, gridTotal / 15);

    for (const auto& g : rawGrids) {
        if (progress && (++gridIndex % gridReportInterval == 0)) {
            progress(80 + gridIndex * 15 / gridTotal);
        }

        glm::dvec3 globalPos = g.xyz;

        if (g.cp != 0) {
            auto it = coordSystems.find(g.cp);
            if (it != coordSystems.end()) {
                const CoordSys& cs = it->second;
                globalPos = cs.origin + cs.R * g.xyz;
            }
        }

        model.addNode(g.id, glm::vec3(
            static_cast<float>(globalPos.x),
            static_cast<float>(globalPos.y),
            static_cast<float>(globalPos.z)));
    }
    if (progress) progress(95);

    qDebug("parseNastranBdf: nodes=%d, elements=%d, coordSystems=%d",
           (int)model.nodes.size(),
           (int)model.elements.size(),
           (int)coordSystems.size());

    // ── 按 Property ID 分组生成部件 ──
    if (!propertyIds.empty()) {
        std::map<int, int> pidToPartIndex;
        for (int pid : propertyIds) {
            int idx = static_cast<int>(model.parts.size());
            FEPart part;
            part.name = "Property " + std::to_string(pid);
            part.visible = true;
            model.parts.push_back(part);
            pidToPartIndex[pid] = idx;
        }
        for (const auto& [eid, pid] : elemPid) {
            auto it = pidToPartIndex.find(pid);
            if (it != pidToPartIndex.end()) {
                model.parts[it->second].elementIds.push_back(eid);
            }
        }
        for (auto& part : model.parts) {
            std::unordered_set<int> nodeSet;
            for (int eid : part.elementIds) {
                auto eit = model.elements.find(eid);
                if (eit != model.elements.end()) {
                    for (int nid : eit->second.nodeIds)
                        nodeSet.insert(nid);
                }
            }
            part.nodeIds.assign(nodeSet.begin(), nodeSet.end());
        }
        qDebug("parseNastranBdf: %d parts generated from Property IDs",
               (int)model.parts.size());
    }

    return true;
}
