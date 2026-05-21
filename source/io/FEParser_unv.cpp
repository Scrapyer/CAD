/**
 * @file FEParser_unv.cpp
 * @brief UNV 结果数据解析实现（Dataset 2414 / 55）
 */

#include "FEParser.h"
#include "FEResultRepository.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>
#include <cmath>

bool FEParser::parseUnvResults(const QString& filePath, FEResultData& results)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("parseUnvResults: cannot open %s", qPrintable(filePath));
        return false;
    }

    QTextStream in(&file);
    results.clear();
    static const QRegularExpression whitespaceRe("\\s+");

    // UNV 结果类型码 → 名称映射
    auto resultTypeName = [](int code) -> std::string {
        switch (code) {
            case 1:  return "General";
            case 2:  return "Stress";
            case 3:  return "Strain";
            case 4:  return "Element Force";
            case 5:  return "Displacement";
            case 6:  return "Stress - Loss of ellipticity indicator";
            case 7:  return "Plastic Strain";
            case 8:  return "Velocity";
            case 9:  return "Acceleration";
            case 10: return "Strain Energy";
            case 11: return "Temperature";
            case 12: return "Heat Flux";
            case 13: return "Heat Gradient";
            case 14: return "Element Stress Resultant";
            case 15: return "Pressure";
            case 16: return "Mass";
            case 17: return "Force";
            case 18: return "Moment";
            case 19: return "Thickness";
            case 20: return "Contact Pressure";
            case 21: return "Reaction Force";
            case 22: return "Kinetic Energy";
            case 23: return "Contact Stress";
            case 24: return "Contact Friction";
            case 25: return "Creep Strain";
            case 26: return "Failure Index";
            case 27: return "Element Force";
            case 28: return "Safety Factor";
            case 29: return "Composite Stress";
            case 30: return "Composite Strain";
            case 31: return "Composite Force";
            case 32: return "Composite Failure";
            case 40: return "Fatigue Life";
            case 41: return "Fatigue Damage";
            case 42: return "Fatigue Safety Factor";
            case 43: return "Biaxiality Ratio";
            case 44: return "Fatigue Equivalent Stress";
            case 45: return "Fatigue Equivalent Strain";
            case 46: return "Fatigue Damage Rate";
            case 47: return "Fatigue Cycles";
            case 48: return "Fatigue Reserve Factor";
            case 50: return "Modal Effective Mass";
            case 51: return "Modal Participation Factor";
            case 55: return "Buckling Factor";
            case 60: return "Acoustic Pressure";
            case 61: return "Acoustic Velocity";
            case 70: return "Wear Depth";
            case 71: return "Wear Rate";
            case 93: return "Unknown Vector";
            case 94: return "Unknown Scalar";
            case 95: return "Unknown 3DOF";
            case 96: return "Unknown 6DOF";
            case 301: return "Damage";
            case 302: return "Life (Repeats)";
            case 303: return "Life (Hours)";
            case 304: return "Log of Life";
            case 305: return "Log of Damage";
            case 306: return "Safety Factor";
            case 307: return "Rainflow Matrix";
            case 308: return "Damage per Bin";
            case 310: return "Crack Initiation Life";
            case 311: return "Crack Propagation Life";
            case 312: return "Total Fatigue Life";
            case 313: return "Stress Intensity Factor";
            case 314: return "J-Integral";
            case 315: return "Crack Length";
            case 401: return "FE-SAFE Life";
            case 402: return "FE-SAFE Damage";
            case 403: return "FE-SAFE Factor of Strength";
            case 410: return "FE-SAFE Multiaxial Life";
            case 411: return "FE-SAFE Multiaxial Damage";
            default: return "Result Type " + std::to_string(code);
        }
    };

    auto componentNames = [](int characteristic, int nvals) -> std::vector<std::string> {
        if (characteristic == 1 || nvals == 1)
            return {"Scalar"};
        if (characteristic == 2 && nvals >= 3)
            return {"X", "Y", "Z"};
        if (characteristic == 3 && nvals >= 6)
            return {"TX", "TY", "TZ", "RX", "RY", "RZ"};
        if (characteristic == 4 && nvals >= 6)
            return {"XX", "YY", "ZZ", "XY", "YZ", "XZ"};
        if (characteristic == 5 && nvals >= 6)
            return {"XX", "YY", "ZZ", "XY", "YZ", "XZ"};
        std::vector<std::string> names;
        for (int i = 0; i < nvals; ++i)
            names.push_back("Component " + std::to_string(i + 1));
        return names;
    };

    auto findOrCreateSubcase = [&](int subcaseId, const std::string& name) -> FESubcase& {
        for (auto& sc : results.subcases) {
            if (sc.id == subcaseId) return sc;
        }
        FESubcase sc;
        sc.id = subcaseId;
        sc.name = name;
        results.subcases.push_back(std::move(sc));
        return results.subcases.back();
    };

    int datasetsParsed = 0;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        if (line != "-1") continue;

        if (in.atEnd()) break;
        line = in.readLine().trimmed();
        bool ok = false;
        int datasetId = line.toInt(&ok);
        if (!ok) {
            while (!in.atEnd()) {
                line = in.readLine().trimmed();
                if (line == "-1") break;
            }
            continue;
        }

        if (datasetId == 2414) {
            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            int datasetLabel = line.toInt();
            (void)datasetLabel;

            if (in.atEnd()) break;
            QString datasetName = in.readLine().trimmed();

            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            int location = line.toInt();

            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            QStringList ids = line.split(whitespaceRe, Qt::SkipEmptyParts);
            if (ids.size() < 6) {
                while (!in.atEnd()) { if (in.readLine().trimmed() == "-1") break; }
                continue;
            }
            int modelType       = ids[0].toInt();
            int analysisType    = ids[1].toInt();
            int dataChar        = ids[2].toInt();
            int resultType      = ids[3].toInt();
            int dataType        = ids[4].toInt();
            int nvalsPerEntity  = ids[5].toInt();
            (void)modelType;

            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            QStringList r5 = line.split(whitespaceRe, Qt::SkipEmptyParts);
            int loadSetId = (r5.size() >= 5) ? r5[4].toInt() : 1;
            int modeNumber = (r5.size() >= 6) ? r5[5].toInt() : 0;
            int timeStepNum = (r5.size() >= 7) ? r5[6].toInt() : 0;

            if (in.atEnd()) break;
            in.readLine();

            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            QStringList r7 = line.split(whitespaceRe, Qt::SkipEmptyParts);
            if (analysisType >= 3 && analysisType <= 7) {
                if (!in.atEnd()) in.readLine();
            }

            std::string subcaseName;
            if (modeNumber > 0) {
                subcaseName = "Mode " + std::to_string(modeNumber);
                if (!r7.isEmpty()) {
                    float freq = r7[0].toFloat();
                    if (freq > 0)
                        subcaseName += " (" + std::to_string(freq) + " Hz)";
                }
            } else if (timeStepNum > 0) {
                subcaseName = "Time Step " + std::to_string(timeStepNum);
                if (!r7.isEmpty()) {
                    float time = r7[0].toFloat();
                    subcaseName += " (t=" + std::to_string(time) + ")";
                }
            } else {
                subcaseName = datasetName.isEmpty()
                    ? ("Load Case " + std::to_string(loadSetId))
                    : datasetName.toStdString();
            }

            int subcaseId = (modeNumber > 0) ? modeNumber : ((timeStepNum > 0) ? timeStepNum : loadSetId);

            FieldLocation fieldLoc = (location == 1) ? FieldLocation::Node : FieldLocation::Element;

            auto compNames = componentNames(dataChar, nvalsPerEntity);
            int numComponents = static_cast<int>(compNames.size());
            if (numComponents > nvalsPerEntity) numComponents = nvalsPerEntity;

            bool isReal = (dataType == 2 || dataType == 5);

            std::vector<std::unordered_map<int, float>> compValues(numComponents);

            bool buildVector = (dataChar == 2 || dataChar == 3) && nvalsPerEntity >= 3;
            std::unordered_map<int, glm::vec3> vectorValues;

            while (!in.atEnd()) {
                line = in.readLine().trimmed();
                if (line == "-1") break;

                QStringList parts = line.split(whitespaceRe, Qt::SkipEmptyParts);
                if (parts.isEmpty()) continue;

                int entityId = parts[0].toInt();

                if (location == 2 && parts.size() >= 2) {
                    int numNodes = parts[1].toInt();
                    std::vector<float> avgVals(nvalsPerEntity, 0.0f);
                    int validNodes = 0;
                    for (int n = 0; n < numNodes && !in.atEnd(); ++n) {
                        line = in.readLine().trimmed();
                        if (line == "-1") break;
                        QStringList vals = line.split(whitespaceRe, Qt::SkipEmptyParts);
                        while (vals.size() < nvalsPerEntity && !in.atEnd()) {
                            QString extra = in.readLine().trimmed();
                            if (extra == "-1") break;
                            vals.append(extra.split(whitespaceRe, Qt::SkipEmptyParts));
                        }
                        if (isReal && vals.size() >= nvalsPerEntity) {
                            for (int v = 0; v < nvalsPerEntity; ++v)
                                avgVals[v] += vals[v].toFloat();
                            validNodes++;
                        }
                    }
                    if (validNodes > 0) {
                        for (int v = 0; v < numComponents; ++v)
                            compValues[v][entityId] = avgVals[v] / validNodes;
                    }
                } else {
                    if (in.atEnd()) break;
                    line = in.readLine().trimmed();
                    if (line == "-1") break;

                    QStringList vals = line.split(whitespaceRe, Qt::SkipEmptyParts);
                    while (vals.size() < nvalsPerEntity && !in.atEnd()) {
                        QString extra = in.readLine().trimmed();
                        if (extra == "-1") break;
                        vals.append(extra.split(whitespaceRe, Qt::SkipEmptyParts));
                    }

                    if (isReal) {
                        for (int v = 0; v < numComponents && v < vals.size(); ++v)
                            compValues[v][entityId] = vals[v].toFloat();

                        if (buildVector && vals.size() >= 3) {
                            vectorValues[entityId] = glm::vec3(
                                vals[0].toFloat(), vals[1].toFloat(), vals[2].toFloat());
                        }
                    }
                }
            }

            if (compValues.empty() || compValues[0].empty()) continue;

            FESubcase& sc = findOrCreateSubcase(subcaseId, subcaseName);

            std::string rtName = resultTypeName(resultType);
            bool isGenericCode = (rtName.find("Result Type ") == 0)
                              || (rtName == "Unknown Scalar")
                              || (rtName == "Unknown Vector")
                              || (rtName == "General");
            if (!datasetName.isEmpty()) {
                if (isGenericCode) {
                    rtName = datasetName.toStdString();
                } else {
                    std::string dsName = datasetName.toStdString();
                    if (dsName != rtName)
                        rtName = dsName + " (" + rtName + ")";
                }
            }

            FEResultType rt;
            rt.name = rtName;

            for (int c = 0; c < numComponents; ++c) {
                FEResultComponent comp;
                comp.name = compNames[c];
                comp.field.name = rtName + " - " + compNames[c];
                comp.field.location = fieldLoc;
                comp.field.values = std::move(compValues[c]);
                rt.components.push_back(std::move(comp));
            }

            if (buildVector && !vectorValues.empty()) {
                rt.hasVector = true;
                rt.vectorField.name = rtName;
                rt.vectorField.location = fieldLoc;
                rt.vectorField.values = vectorValues;

                FEResultComponent magComp;
                magComp.name = "Magnitude";
                magComp.field.name = rtName + " - Magnitude";
                magComp.field.location = fieldLoc;
                for (const auto& [id, vec] : vectorValues) {
                    magComp.field.values[id] = glm::length(vec);
                }
                rt.components.insert(rt.components.begin(), std::move(magComp));
            }

            sc.resultTypes.push_back(std::move(rt));
            datasetsParsed++;

        } else if (datasetId == 55) {
            if (in.atEnd()) break;
            line = in.readLine().trimmed();
            QStringList r1 = line.split(whitespaceRe, Qt::SkipEmptyParts);
            if (r1.size() < 2) {
                while (!in.atEnd()) { if (in.readLine().trimmed() == "-1") break; }
                continue;
            }

            if (in.atEnd()) break;
            line = in.readLine().trimmed();

            if (in.atEnd()) break;
            line = in.readLine().trimmed();

            int datasetLabel = r1.size() >= 1 ? r1[0].toInt() : 0;
            int subcaseId = datasetLabel > 0 ? datasetLabel : 1;
            if (in.atEnd()) break;
            in.readLine();
            if (in.atEnd()) break;
            in.readLine();

            if (in.atEnd()) break;
            line = in.readLine().trimmed();

            std::unordered_map<int, float> magValues;
            std::unordered_map<int, float> xValues, yValues, zValues;
            std::unordered_map<int, glm::vec3> vecValues;

            while (!in.atEnd()) {
                line = in.readLine().trimmed();
                if (line == "-1") break;

                QStringList parts = line.split(whitespaceRe, Qt::SkipEmptyParts);
                if (parts.isEmpty()) continue;
                int nodeId = parts[0].toInt();

                if (in.atEnd()) break;
                line = in.readLine().trimmed();
                if (line == "-1") break;

                QStringList vals = line.split(whitespaceRe, Qt::SkipEmptyParts);
                if (vals.size() >= 3) {
                    float vx = vals[0].toFloat();
                    float vy = vals[1].toFloat();
                    float vz = vals[2].toFloat();
                    float mag = std::sqrt(vx*vx + vy*vy + vz*vz);
                    xValues[nodeId] = vx;
                    yValues[nodeId] = vy;
                    zValues[nodeId] = vz;
                    magValues[nodeId] = mag;
                    vecValues[nodeId] = glm::vec3(vx, vy, vz);
                }
            }

            if (magValues.empty()) continue;

            FESubcase& sc = findOrCreateSubcase(subcaseId,
                "Load Case " + std::to_string(subcaseId));

            FEResultType rt;
            rt.name = "Displacement";
            rt.hasVector = true;
            rt.vectorField.name = "Displacement";
            rt.vectorField.location = FieldLocation::Node;
            rt.vectorField.values = vecValues;

            auto makeComp = [&](const std::string& name,
                                std::unordered_map<int, float>& vals) {
                FEResultComponent comp;
                comp.name = name;
                comp.field.name = "Displacement - " + name;
                comp.field.location = FieldLocation::Node;
                comp.field.values = std::move(vals);
                return comp;
            };

            rt.components.push_back(makeComp("Magnitude", magValues));
            rt.components.push_back(makeComp("X", xValues));
            rt.components.push_back(makeComp("Y", yValues));
            rt.components.push_back(makeComp("Z", zValues));

            sc.resultTypes.push_back(std::move(rt));
            datasetsParsed++;

        } else {
            while (!in.atEnd()) {
                line = in.readLine().trimmed();
                if (line == "-1") break;
            }
        }
    }

    return datasetsParsed > 0;
}

bool FEParser::parseUnvResults(const QString& filePath, FEResultRepository& repo)
{
    FEResultData data;
    if (!parseUnvResults(filePath, data))
        return false;

    repo.clear();
    for (const auto& sc : data.subcases) {
        FEResultFrame frame;
        frame.info.subcaseId = sc.id;
        frame.info.stepIndex = 0;
        frame.info.frameIndex = 0;
        frame.info.valueLabel = sc.name;
        frame.resultTypes = sc.resultTypes;

        std::string name = sc.name;
        if (name.find("Mode ") == 0) {
            frame.info.domain = FEResultDomain::Mode;
            auto paren = name.find('(');
            if (paren != std::string::npos) {
                auto end = name.find(" Hz", paren);
                if (end != std::string::npos) {
                    std::string freqStr = name.substr(paren + 1, end - paren - 1);
                    try { frame.info.value = std::stod(freqStr); } catch (...) {}
                }
            }
        } else if (name.find("Time Step ") == 0) {
            frame.info.domain = FEResultDomain::Time;
            auto eq = name.find("t=");
            if (eq != std::string::npos) {
                auto end = name.find(')', eq);
                if (end != std::string::npos) {
                    std::string timeStr = name.substr(eq + 2, end - eq - 2);
                    try { frame.info.value = std::stod(timeStr); } catch (...) {}
                }
            }
        } else {
            frame.info.domain = FEResultDomain::Static;
            frame.info.value = 0.0;
        }

        repo.addFrame(frame);
    }
    return true;
}
