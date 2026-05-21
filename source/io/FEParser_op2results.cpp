/**
 * @file FEParser_op2results.cpp
 * @brief Nastran OP2 结果数据解析实现（位移 OUG + 应力 OES）
 */

#include "FEParser.h"
#include "FEResultRepository.h"

#include <QFile>
#include <QDebug>
#include <glm/glm.hpp>

#include <map>
#include <cstring>
#include <cmath>

bool FEParser::parseNastranOp2Results(const QString& filePath, FEResultData& results)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("parseNastranOp2Results: cannot open %s", qPrintable(filePath));
        return false;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 48) return false;

    // ── 字节序检测 ──
    bool needSwap = false;
    {
        quint32 first4;
        file.read(reinterpret_cast<char*>(&first4), 4);
        file.seek(0);
        if (first4 == 4) {
            needSwap = false;
        } else {
            quint32 swapped = ((first4 >> 24) & 0xFF) |
                              ((first4 >> 8)  & 0xFF00) |
                              ((first4 << 8)  & 0xFF0000) |
                              ((first4 << 24) & 0xFF000000);
            if (swapped == 4) needSwap = true;
            else return false;
        }
    }

    // ── 辅助 lambda ──
    auto swapBytes32 = [](quint32 v) -> quint32 {
        return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
               ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    };

    auto readInt32 = [&]() -> qint32 {
        quint32 v;
        file.read(reinterpret_cast<char*>(&v), 4);
        if (needSwap) v = swapBytes32(v);
        return static_cast<qint32>(v);
    };

    auto readFortranRecord = [&]() -> QByteArray {
        qint32 len = readInt32();
        if (len < 0) len = -len;
        QByteArray data = file.read(len);
        readInt32();
        return data;
    };

    auto readMarker = [&]() -> qint32 {
        QByteArray rec = readFortranRecord();
        if (rec.size() < 4) return 0;
        quint32 raw;
        memcpy(&raw, rec.constData(), 4);
        if (needSwap) raw = swapBytes32(raw);
        return static_cast<qint32>(raw);
    };

    auto skipTable = [&]() {
        readMarker();
        readFortranRecord();
        readMarker();
        while (true) {
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                readFortranRecord();
            }
            qint32 m = readMarker();
            if (m == 0) break;
            readFortranRecord();
        }
    };

    auto readFloat = [&](const char* ptr) -> float {
        quint32 raw;
        memcpy(&raw, ptr, 4);
        if (needSwap) raw = swapBytes32(raw);
        float val;
        memcpy(&val, &raw, 4);
        return val;
    };

    auto readInt = [&](const char* ptr) -> qint32 {
        quint32 raw;
        memcpy(&raw, ptr, 4);
        if (needSwap) raw = swapBytes32(raw);
        return static_cast<qint32>(raw);
    };

    // ── 跳过文件头 ──
    {
        qint32 firstVal = readMarker();
        if (firstVal == 3) {
            readFortranRecord();
            readMarker();
            readFortranRecord();
            while (!file.atEnd()) {
                qint32 m = readMarker();
                if (m == -1) break;
                readFortranRecord();
            }
            readMarker();
        } else {
            file.seek(0);
        }
    }

    results.clear();
    std::map<int, int> subcaseMap;

    bool foundAny = false;

    // ── 表级导航循环 ──
    while (!file.atEnd()) {
        qint32 nameMarker = readMarker();
        if (file.atEnd() || nameMarker == 0) break;

        QByteArray nameRec = readFortranRecord();
        QString tableName = QString::fromLatin1(nameRec.left(8)).trimmed();

        readMarker();

        bool isOUG = tableName.startsWith("OUG") || tableName.startsWith("OUGV");
        bool isOES = tableName.startsWith("OES");

        if (!isOUG && !isOES) {
            skipTable();
            continue;
        }

        qDebug("parseNastranOp2Results: found result table '%s'", qPrintable(tableName));

        readMarker();
        readFortranRecord();
        readMarker();

        std::vector<QByteArray> subtables;
        bool hasFirstBlock = false;
        QByteArray firstBlock;
        while (!file.atEnd()) {
            QByteArray stData;
            if (hasFirstBlock) {
                stData.append(firstBlock);
                hasFirstBlock = false;
            }
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                stData.append(readFortranRecord());
            }
            subtables.push_back(stData);
            qint32 nextM = readMarker();
            if (nextM == 0) break;
            firstBlock = readFortranRecord();
            hasFirstBlock = true;
        }

        const int IDENT_SIZE = 588;
        (void)IDENT_SIZE;

        auto getOrCreateSubcase = [&](int scId) -> FESubcase& {
            auto it = subcaseMap.find(scId);
            if (it == subcaseMap.end()) {
                int idx = static_cast<int>(results.subcases.size());
                FESubcase sc;
                sc.id = scId;
                sc.name = "Subcase " + std::to_string(scId);
                results.subcases.push_back(sc);
                subcaseMap[scId] = idx;
                return results.subcases[idx];
            }
            return results.subcases[it->second];
        };

        if (isOUG) {
            for (int si = 1; si + 1 < static_cast<int>(subtables.size()); si += 2) {
                const auto& identRec = subtables[si];
                const auto& dataRec  = subtables[si + 1];

                if (identRec.size() < 16) continue;
                int subcaseId = readInt(identRec.constData() + 12);
                if (subcaseId <= 0) subcaseId = 1;

                if (dataRec.size() < 32) continue;

                FESubcase& subcase = getOrCreateSubcase(subcaseId);

                FEResultType dispType;
                dispType.name = "Displacement";
                dispType.hasVector = true;

                FEResultComponent compMag, compX, compY, compZ;
                compMag.name = "Magnitude"; compMag.field.name = "Displacement Magnitude";
                compMag.field.unit = "mm";   compMag.field.location = FieldLocation::Node;
                compX.name = "X"; compX.field.name = "Displacement X";
                compX.field.unit = "mm"; compX.field.location = FieldLocation::Node;
                compY.name = "Y"; compY.field.name = "Displacement Y";
                compY.field.unit = "mm"; compY.field.location = FieldLocation::Node;
                compZ.name = "Z"; compZ.field.name = "Displacement Z";
                compZ.field.unit = "mm"; compZ.field.location = FieldLocation::Node;

                dispType.vectorField.name = "Displacement";
                dispType.vectorField.unit = "mm";
                dispType.vectorField.location = FieldLocation::Node;

                int parsed = 0;
                const char* ptr = dataRec.constData();
                int nBytes = dataRec.size();

                for (int offset = 0; offset + 32 <= nBytes; offset += 32) {
                    qint32 nid = readInt(ptr + offset);
                    if (nid <= 0 || nid > 100000000) continue;

                    float tx = readFloat(ptr + offset + 8);
                    float ty = readFloat(ptr + offset + 12);
                    float tz = readFloat(ptr + offset + 16);
                    float mag = std::sqrt(tx*tx + ty*ty + tz*tz);

                    compMag.field.values[nid] = mag;
                    compX.field.values[nid] = tx;
                    compY.field.values[nid] = ty;
                    compZ.field.values[nid] = tz;
                    dispType.vectorField.values[nid] = glm::vec3(tx, ty, tz);
                    parsed++;
                }

                if (parsed > 0) {
                    dispType.components.push_back(std::move(compMag));
                    dispType.components.push_back(std::move(compX));
                    dispType.components.push_back(std::move(compY));
                    dispType.components.push_back(std::move(compZ));
                    subcase.resultTypes.push_back(std::move(dispType));
                    foundAny = true;
                    qDebug("parseNastranOp2Results: parsed %d disp nodes (subcase %d)",
                           parsed, subcaseId);
                }
            }

        } else if (isOES) {
            for (int si = 1; si + 1 < static_cast<int>(subtables.size()); si += 2) {
                const auto& identRec = subtables[si];
                const auto& dataRec  = subtables[si + 1];

                if (identRec.size() < 16 || dataRec.size() < 32) continue;

                int elemTypeCode = readInt(identRec.constData() + 8);
                int subcaseId    = readInt(identRec.constData() + 12);
                if (subcaseId <= 0) subcaseId = 1;

                // Rod 类单元：CROD(1), CBEAM(2), CTUBE(3), CONROD(10), CBAR(34/100)
                bool isRod = (elemTypeCode == 1 || elemTypeCode == 2 ||
                              elemTypeCode == 3 || elemTypeCode == 10 ||
                              elemTypeCode == 34 || elemTypeCode == 100);
                // Shell 类单元：CQUAD4(33), CTRIA3(74), CTRIA6(75), CQUAD4(144),
                //   CQUAD4 composite(95), CTRIA3 composite(97),
                //   CTRIA3 nonlinear(88), CQUAD8(64)
                bool isShell = (elemTypeCode == 33 || elemTypeCode == 74 ||
                               elemTypeCode == 75 || elemTypeCode == 144 ||
                               elemTypeCode == 95 || elemTypeCode == 97 ||
                               elemTypeCode == 88 || elemTypeCode == 64);
                // Solid 类单元：CTETRA(39/85), CHEXA(67/86/93), CPENTA(68/91)
                bool isSolid = (elemTypeCode == 39 || elemTypeCode == 67 ||
                               elemTypeCode == 68 || elemTypeCode == 85 ||
                               elemTypeCode == 86 || elemTypeCode == 93 ||
                               elemTypeCode == 91);
                // Spring/Bush 类：CELAS(11/12), CBUSH(102)
                bool isSpring = (elemTypeCode == 11 || elemTypeCode == 12 ||
                                 elemTypeCode == 102);

                if (!isRod && !isShell && !isSolid && !isSpring) {
                    qDebug("parseNastranOp2Results: skipping OES elem type %d (subcase %d)",
                           elemTypeCode, subcaseId);
                    continue;
                }

                FESubcase& subcase = getOrCreateSubcase(subcaseId);

                FEResultType* stressPtr = nullptr;
                for (auto& rt : subcase.resultTypes) {
                    if (rt.name == "Stress") { stressPtr = &rt; break; }
                }
                if (!stressPtr) {
                    FEResultType stressType;
                    stressType.name = "Stress";
                    stressType.hasVector = false;
                    subcase.resultTypes.push_back(std::move(stressType));
                    stressPtr = &subcase.resultTypes.back();
                }

                const char* ptr = dataRec.constData();
                int nBytes = dataRec.size();
                int parsed = 0;

                if (isRod) {
                    int entrySize = 20;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iAxial   = ensureCompIdx("Axial",   "Axial Stress");
                    int iTorsion = ensureCompIdx("Torsion", "Torsional Stress");

                    for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) continue;

                        stressPtr->components[iAxial].field.values[eid]   = readFloat(ptr + offset + 4);
                        stressPtr->components[iTorsion].field.values[eid] = readFloat(ptr + offset + 12);
                        parsed++;
                    }
                } else if (isShell) {
                    int entrySize = 68;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iSx  = ensureCompIdx("Sigma-X",   "Normal Stress X");
                    int iSy  = ensureCompIdx("Sigma-Y",   "Normal Stress Y");
                    int iTxy = ensureCompIdx("Tau-XY",     "Shear Stress XY");
                    int iVm  = ensureCompIdx("Von Mises",  "Von Mises Stress");

                    for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) continue;

                        stressPtr->components[iSx].field.values[eid]  = readFloat(ptr + offset + 8);
                        stressPtr->components[iSy].field.values[eid]  = readFloat(ptr + offset + 12);
                        stressPtr->components[iTxy].field.values[eid] = readFloat(ptr + offset + 16);
                        stressPtr->components[iVm].field.values[eid]  = readFloat(ptr + offset + 32);
                        parsed++;
                    }
                } else if (isSolid) {
                    // 实体单元节点数映射
                    int nnodes = 0;
                    if (elemTypeCode == 67 || elemTypeCode == 86)      nnodes = 8;  // CHEXA
                    else if (elemTypeCode == 39 || elemTypeCode == 85) nnodes = 4;  // CTETRA
                    else if (elemTypeCode == 68 || elemTypeCode == 91) nnodes = 6;  // CPENTA
                    else if (elemTypeCode == 93)                       nnodes = 8;  // CHEXA nonlinear
                    int entrySize = 44 + nnodes * 40;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iSxx = ensureCompIdx("Sigma-XX", "Normal Stress XX");
                    int iSyy = ensureCompIdx("Sigma-YY", "Normal Stress YY");
                    int iSzz = ensureCompIdx("Sigma-ZZ", "Normal Stress ZZ");
                    int iTxy = ensureCompIdx("Tau-XY",   "Shear Stress XY");
                    int iTyz = ensureCompIdx("Tau-YZ",   "Shear Stress YZ");
                    int iTxz = ensureCompIdx("Tau-XZ",   "Shear Stress XZ");
                    int iVm  = ensureCompIdx("Von Mises","Von Mises Stress");

                    int offset = 0;
                    while (offset + entrySize <= nBytes) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) break;

                        stressPtr->components[iSxx].field.values[eid] = readFloat(ptr + offset + 8);
                        stressPtr->components[iSyy].field.values[eid] = readFloat(ptr + offset + 12);
                        stressPtr->components[iSzz].field.values[eid] = readFloat(ptr + offset + 16);
                        stressPtr->components[iTxy].field.values[eid] = readFloat(ptr + offset + 20);
                        stressPtr->components[iTyz].field.values[eid] = readFloat(ptr + offset + 24);
                        stressPtr->components[iTxz].field.values[eid] = readFloat(ptr + offset + 28);
                        stressPtr->components[iVm].field.values[eid]  = readFloat(ptr + offset + 32);
                        parsed++;
                        offset += entrySize;
                    }
                } else if (isSpring) {
                    // Spring/Bush 类单元：简单标量力/应力
                    // CELAS(11/12): 8 bytes per entry (eid + stress)
                    // CBUSH(102): 28 bytes per entry (eid + 6 forces/moments)
                    int entrySize = (elemTypeCode == 102) ? 28 : 8;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };

                    if (elemTypeCode == 102) {
                        // CBUSH: Fx, Fy, Fz, Mx, My, Mz
                        int iFx = ensureCompIdx("Fx", "Bush Force X");
                        int iFy = ensureCompIdx("Fy", "Bush Force Y");
                        int iFz = ensureCompIdx("Fz", "Bush Force Z");

                        for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                            qint32 eid = readInt(ptr + offset);
                            if (eid <= 0 || eid > 100000000) continue;
                            stressPtr->components[iFx].field.values[eid] = readFloat(ptr + offset + 4);
                            stressPtr->components[iFy].field.values[eid] = readFloat(ptr + offset + 8);
                            stressPtr->components[iFz].field.values[eid] = readFloat(ptr + offset + 12);
                            parsed++;
                        }
                    } else {
                        // CELAS: stress scalar
                        int iStress = ensureCompIdx("Spring Stress", "Spring Stress");

                        for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                            qint32 eid = readInt(ptr + offset);
                            if (eid <= 0 || eid > 100000000) continue;
                            stressPtr->components[iStress].field.values[eid] = readFloat(ptr + offset + 4);
                            parsed++;
                        }
                    }
                }

                if (parsed > 0) {
                    foundAny = true;
                    qDebug("parseNastranOp2Results: parsed %d stress elems (subcase %d, elemType %d)",
                           parsed, subcaseId, elemTypeCode);
                }
            }
        }
    }

    file.close();

    if (foundAny) {
        qDebug("parseNastranOp2Results: %d subcases loaded", (int)results.subcases.size());
    }

    return foundAny;
}

bool FEParser::parseNastranOp2Results(const QString& filePath, FEResultRepository& repo)
{
    FEResultData data;
    if (!parseNastranOp2Results(filePath, data))
        return false;
    repo = FEResultRepository::fromResultData(data);
    return true;
}
