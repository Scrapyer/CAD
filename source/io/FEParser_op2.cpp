/**
 * @file FEParser_op2.cpp
 * @brief Nastran OP2 二进制文件几何解析实现
 */

#include "FEParser.h"

#include <QFile>
#include <QDebug>
#include <glm/glm.hpp>

#include <set>
#include <map>
#include <unordered_set>
#include <cstring>

bool FEParser::parseNastranOp2(const QString& filePath, FEModel& model,
                                const std::function<void(int)>& progress)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("parseNastranOp2: cannot open %s", qPrintable(filePath));
        return false;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 48) {
        qWarning("parseNastranOp2: file too small");
        return false;
    }

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
            if (swapped == 4) {
                needSwap = true;
            } else {
                qWarning("parseNastranOp2: not a valid OP2 file (first 4 bytes: 0x%08X)", first4);
                return false;
            }
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
        readInt32(); // trailing length
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

    auto readSubtableData = [&]() -> QByteArray {
        QByteArray allData;
        while (true) {
            qint32 m = readMarker();
            if (m == 0) break;
            QByteArray rec = readFortranRecord();
            allData.append(rec);
        }
        return allData;
    };
    (void)readSubtableData; // 保留备用

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

    // ── 收集数据 ──
    std::set<int> propertyIds;
    std::map<int, int> elemPid;

    std::vector<glm::vec3> bgpdtCoords;
    std::map<int, int> internalToExternal;

    // ── 表级导航循环 ──
    while (!file.atEnd()) {
        if (progress) {
            int pct = static_cast<int>(file.pos() * 80 / fileSize);
            progress(pct);
        }

        qint32 nameMarker = readMarker();
        if (file.atEnd() || nameMarker == 0) break;

        QByteArray nameRec = readFortranRecord();
        QString tableName = QString::fromLatin1(nameRec.left(8)).trimmed();

        readMarker();

        qDebug("parseNastranOp2: table '%s' at pos %lld", qPrintable(tableName), file.pos());

        bool isGeom1 = tableName.startsWith("GEOM1") || tableName == "GEOM1S";
        bool isGeom2 = tableName.startsWith("GEOM2") || tableName == "GEOM2S";
        bool isBgpdt = tableName.startsWith("BGPDT");
        bool isEqexin = tableName.startsWith("EQEXIN");

        if (!isGeom1 && !isGeom2 && !isBgpdt && !isEqexin) {
            skipTable();
            continue;
        }

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
                qint64 beforePos = file.pos();
                qint32 m = readMarker();
                if (m == 0) break;
                QByteArray rec = readFortranRecord();
                if (rec.isEmpty()) {
                    qDebug("parseNastranOp2: empty record at pos %lld (marker=%d), breaking", beforePos, m);
                    break;
                }
                stData.append(rec);
            }
            subtables.push_back(stData);
            qint32 nextM = readMarker();
            if (nextM == 0) break;
            firstBlock = readFortranRecord();
            if (firstBlock.isEmpty()) {
                qDebug("parseNastranOp2: empty firstBlock (nextM=%d), breaking", nextM);
                break;
            }
            hasFirstBlock = true;
        }

        if (isGeom1) {
            for (const auto& stData : subtables) {
                int nWords = stData.size() / 4;
                if (nWords < 11) continue;

                const qint32* words = reinterpret_cast<const qint32*>(stData.constData());

                if (words[0] != 4501) continue;

                int dataStart = 2;
                if (nWords > 3 && words[2] > 0 && words[2] <= 100000000) {
                    int rem2 = (nWords - 2) % 8;
                    int rem3 = (nWords - 3) % 8;
                    if (rem3 <= 1 && rem2 > 1) {
                        dataStart = 3;
                    }
                }

                int parsed = 0;
                for (int i = dataStart; i + 8 <= nWords; i += 8) {
                    qint32 nid = words[i];
                    if (nid <= 0) break;

                    quint32 rawX, rawY, rawZ;
                    memcpy(&rawX, &words[i + 2], 4);
                    memcpy(&rawY, &words[i + 3], 4);
                    memcpy(&rawZ, &words[i + 4], 4);

                    if (needSwap) {
                        rawX = swapBytes32(rawX);
                        rawY = swapBytes32(rawY);
                        rawZ = swapBytes32(rawZ);
                    }

                    float x, y, z;
                    memcpy(&x, &rawX, 4);
                    memcpy(&y, &rawY, 4);
                    memcpy(&z, &rawZ, 4);

                    model.addNode(nid, glm::vec3(x, y, z));
                    parsed++;
                }
                if (parsed > 0) {
                    qDebug("parseNastranOp2: parsed %d GRID nodes (dataStart=%d)", parsed, dataStart);
                }
            }
        }

        if (isGeom2) {
            struct ElemInfo {
                ElementType type;
                int nodeCount;
            };
            std::map<int, ElemInfo> elemKeyMap = {
                {2408,  {ElementType::BAR2,     2}},
                {3001,  {ElementType::BAR2,     2}},
                {5408,  {ElementType::BAR2,     2}},
                {5959,  {ElementType::TRI3,     3}},
                {4801,  {ElementType::TRI6,     6}},
                {2958,  {ElementType::QUAD4,    4}},
                {4701,  {ElementType::QUAD8,    8}},
                {5508,  {ElementType::TET4,     4}},
                {16600, {ElementType::TET10,   10}},
                {7308,  {ElementType::HEX8,     8}},
                {16300, {ElementType::HEX20,   20}},
                {4108,  {ElementType::WEDGE6,   6}},
                {16500, {ElementType::WEDGE6,   6}},
                {17200, {ElementType::PYRAMID5,  5}},
            };

            for (const auto& stData : subtables) {
                int nWords = stData.size() / 4;
                if (nWords < 6) continue;

                const qint32* words = reinterpret_cast<const qint32*>(stData.constData());

                qint32 key1 = words[0];
                auto it = elemKeyMap.find(key1);
                if (it == elemKeyMap.end()) continue;

                const ElemInfo& info = it->second;

                int dataStart = 0;
                int stride = 0;

                for (int tryStart = 3; tryStart >= 2 && stride == 0; --tryStart) {
                    if (tryStart >= nWords) continue;
                    qint32 tryEid = words[tryStart];
                    if (tryEid <= 0 || tryEid > 100000000) continue;

                    for (int s = info.nodeCount + 2; s <= std::min(30, nWords - tryStart - 1); ++s) {
                        int idx = tryStart + s;
                        if (idx + 1 >= nWords) break;
                        qint32 eid2 = words[idx];
                        qint32 pid2 = words[idx + 1];
                        if (eid2 > tryEid && eid2 <= tryEid + 100000 &&
                            pid2 > 0 && pid2 <= 100000000) {
                            int idx3 = tryStart + s * 2;
                            if (idx3 + 1 < nWords) {
                                qint32 eid3 = words[idx3];
                                if (eid3 <= eid2 || eid3 > eid2 + 100000) continue;
                            }
                            dataStart = tryStart;
                            stride = s;
                            break;
                        }
                    }
                }
                if (stride == 0) continue;

                qint32 firstEid = words[dataStart];
                (void)firstEid;

                int dataWords = nWords - dataStart;
                int maxEntries = dataWords / stride;
                int count = 0;

                for (int e = 0; e < maxEntries; ++e) {
                    int base = dataStart + e * stride;
                    if (base + 2 + info.nodeCount > nWords) break;

                    qint32 eid = words[base];
                    qint32 pid = words[base + 1];
                    if (eid <= 0) break;

                    std::vector<int> nodeIds(info.nodeCount);
                    for (int ni = 0; ni < info.nodeCount; ++ni) {
                        nodeIds[ni] = words[base + 2 + ni];
                    }

                    model.addElement(eid, info.type, nodeIds);
                    elemPid[eid] = pid;
                    propertyIds.insert(pid);
                    count++;
                }

                qDebug("parseNastranOp2: parsed %d elements (key=%d, stride=%d, nodes=%d)",
                       count, key1, stride, info.nodeCount);
            }
        }

        if (isBgpdt) {
            for (int si = 1; si < static_cast<int>(subtables.size()); si++) {
                const auto& stData = subtables[si];
                const char* ptr = stData.constData();

                for (int off = 0; off + 16 <= stData.size(); off += 16) {
                    float x = readFloat(ptr + off + 4);
                    float y = readFloat(ptr + off + 8);
                    float z = readFloat(ptr + off + 12);
                    bgpdtCoords.push_back(glm::vec3(x, y, z));
                }
            }
            if (!bgpdtCoords.empty()) {
                qDebug("parseNastranOp2: BGPDT parsed %d node coords", (int)bgpdtCoords.size());
            }
        }

        if (isEqexin) {
            for (int si = 1; si < static_cast<int>(subtables.size()); si++) {
                const auto& stData = subtables[si];
                const char* ptr = stData.constData();

                for (int off = 0; off + 8 <= stData.size(); off += 8) {
                    qint32 extId = readInt(ptr + off);
                    qint32 intId = readInt(ptr + off + 4);
                    if (extId > 0 && intId > 0) {
                        internalToExternal[intId] = extId;
                    }
                }
            }
            if (!internalToExternal.empty()) {
                qDebug("parseNastranOp2: EQEXIN parsed %d ID mappings", (int)internalToExternal.size());
            }
        }
    }

    // ── 如果 GEOM1 没有节点但 BGPDT 有，使用 BGPDT 数据 ──
    if (model.nodes.empty() && !bgpdtCoords.empty()) {
        for (int i = 0; i < static_cast<int>(bgpdtCoords.size()); i++) {
            int intId = i + 1;
            int extId = intId;
            auto it = internalToExternal.find(intId);
            if (it != internalToExternal.end()) {
                extId = it->second;
            }
            model.addNode(extId, bgpdtCoords[i]);
        }
        qDebug("parseNastranOp2: added %d nodes from BGPDT", (int)bgpdtCoords.size());
    }

    file.close();

    // ── NX Nastran 嵌套表回退扫描 ──
    if (model.nodes.empty()) {
        qDebug("parseNastranOp2: no nodes from standard tables, scanning for embedded GRID data...");
        QFile file2(filePath);
        if (file2.open(QIODevice::ReadOnly)) {
            QByteArray allData = file2.readAll();
            file2.close();
            const char* raw = allData.constData();
            int fileLen = allData.size();

            auto swapBytes32Local = [](quint32 v) -> quint32 {
                return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                       ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
            };

            for (int offset = 0; offset < fileLen - 16; offset += 4) {
                qint32 recLen;
                memcpy(&recLen, raw + offset, 4);
                if (needSwap) recLen = static_cast<qint32>(swapBytes32Local(static_cast<quint32>(recLen)));
                if (recLen < 32 || recLen > 10000000) continue;
                if (offset + 4 + recLen + 4 > fileLen) continue;

                qint32 tailLen;
                memcpy(&tailLen, raw + offset + 4 + recLen, 4);
                if (needSwap) tailLen = static_cast<qint32>(swapBytes32Local(static_cast<quint32>(tailLen)));
                if (tailLen != recLen) continue;

                const char* recData = raw + offset + 4;
                qint32 key1, key2;
                memcpy(&key1, recData, 4);
                memcpy(&key2, recData + 4, 4);
                if (needSwap) {
                    key1 = static_cast<qint32>(swapBytes32Local(static_cast<quint32>(key1)));
                    key2 = static_cast<qint32>(swapBytes32Local(static_cast<quint32>(key2)));
                }
                if (key1 != 4501 || (key2 != 40 && key2 != 45)) continue;

                int nWords = recLen / 4;
                int dataStart = (key2 == 45) ? 3 : 2;
                int parsed = 0;

                for (int i = dataStart; i + 8 <= nWords; i += 8) {
                    qint32 nid;
                    memcpy(&nid, recData + i * 4, 4);
                    if (needSwap) nid = static_cast<qint32>(swapBytes32Local(static_cast<quint32>(nid)));
                    if (nid <= 0 || nid > 100000000) break;

                    quint32 rawX, rawY, rawZ;
                    memcpy(&rawX, recData + (i + 2) * 4, 4);
                    memcpy(&rawY, recData + (i + 3) * 4, 4);
                    memcpy(&rawZ, recData + (i + 4) * 4, 4);
                    if (needSwap) {
                        rawX = swapBytes32Local(rawX);
                        rawY = swapBytes32Local(rawY);
                        rawZ = swapBytes32Local(rawZ);
                    }

                    float x, y, z;
                    memcpy(&x, &rawX, 4);
                    memcpy(&y, &rawY, 4);
                    memcpy(&z, &rawZ, 4);

                    model.addNode(nid, glm::vec3(x, y, z));
                    parsed++;
                }
                if (parsed > 0) {
                    qDebug("parseNastranOp2: embedded scan found %d GRID nodes (key2=%d, offset=%d)",
                           parsed, key2, offset);
                }
            }
        }
    }

    if (progress) progress(85);

    qDebug("parseNastranOp2: nodes=%d, elements=%d",
           (int)model.nodes.size(), (int)model.elements.size());

    // ── 按 Property ID 分组生成部件 ──（85-95%）
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
        qDebug("parseNastranOp2: %d parts generated from Property IDs",
               (int)model.parts.size());
    }
    if (progress) progress(95);

    if (model.nodes.empty()) {
        qWarning("parseNastranOp2: no nodes found in file");
        return false;
    }

    if (progress) progress(100);
    return true;
}
