#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <map>
#include <vector>
#include <glm/glm.hpp>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    QString filePath = "/Users/zhangkaiyuan/Downloads/Abaqus-VUMAT-Isotropic-Elasticity-Isothermal-Suboutine-main/AbaqusMaterialModel.inp";
    
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Cannot open file:" << filePath;
        return 1;
    }
    
    qDebug() << "File opened successfully";
    
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
    
    enum Section { None, Node, Element } section = None;
    int nodeCount = 0, elemCount = 0;
    int expectedNodeCount = 8;
    int pendingElemId = -1;
    std::vector<int> pendingNodeIds;
    
    auto flushPending = [&]() {
        if (pendingElemId >= 0 && !pendingNodeIds.empty()) {
            elemCount++;
            pendingElemId = -1;
            pendingNodeIds.clear();
        }
    };
    
    QTextStream stream(&f);
    int lineNum = 0;
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNum++;
        
        if (line.isEmpty() || line.startsWith("**")) continue;
        
        if (line.startsWith("*")) {
            flushPending();
            QString upper = line.toUpper();
            
            if (upper.startsWith("*NODE") && !upper.startsWith("*NSET")
                && !upper.contains("OUTPUT")) {
                section = Node;
                qDebug() << "Line" << lineNum << ": Entering NODE section";
            } else if (upper.startsWith("*ELEMENT") && upper.contains("TYPE")
                       && !upper.contains("OUTPUT")) {
                section = Element;
                static const QRegularExpression rx(
                    "TYPE\\s*=\\s*([A-Za-z0-9]+)",
                    QRegularExpression::CaseInsensitiveOption);
                QString elemType = "?";
                const QRegularExpressionMatch match = rx.match(line);
                if (match.hasMatch()) {
                    elemType = match.captured(1);
                }
                qDebug() << "Line" << lineNum << ": Entering ELEMENT section, type=" << elemType;
                expectedNodeCount = 8; // C3D8
            } else {
                if (section != None)
                    qDebug() << "Line" << lineNum << ": Leaving section, keyword:" << line.left(30);
                section = None;
            }
            continue;
        }
        
        if (section == Node) {
            QStringList parts = splitLine(line);
            if (parts.size() >= 4) {
                nodeCount++;
            }
        } else if (section == Element) {
            QStringList parts = splitLine(line);
            if (parts.isEmpty()) continue;
            
            if (pendingElemId < 0) {
                pendingElemId = parts[0].toInt();
                for (int i = 1; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            } else {
                for (int i = 0; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            }
            
            if ((int)pendingNodeIds.size() >= expectedNodeCount)
                flushPending();
        }
    }
    
    flushPending();
    
    qDebug() << "=== RESULT ===";
    qDebug() << "Nodes:" << nodeCount;
    qDebug() << "Elements:" << elemCount;
    
    return 0;
}
