/**
 * @file ImportPathState.h
 * @brief 文件导入面板的路径状态
 *
 * 只记录模型/结果路径之间的 UI 关系，不参与解析和渲染。
 */

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

struct ImportPathState {
    QString modelPath;
    QString resultPath;
    bool resultPathAutoFilled = false;

    static bool isOp2Path(const QString& path)
    {
        return QFileInfo(path.trimmed()).suffix().compare("op2", Qt::CaseInsensitive) == 0;
    }

    static bool samePath(const QString& lhs, const QString& rhs)
    {
        return QDir::cleanPath(lhs.trimmed()) == QDir::cleanPath(rhs.trimmed());
    }

    static bool looksAutoFilled(const QString& model, const QString& result)
    {
        return isOp2Path(model) && !result.trimmed().isEmpty() && samePath(model, result);
    }

    void restore(const QString& model, const QString& result, bool autoFilled)
    {
        modelPath = model.trimmed();
        resultPath = result.trimmed();
        resultPathAutoFilled = autoFilled && looksAutoFilled(modelPath, resultPath);
    }

    void selectModelFile(const QString& path)
    {
        modelPath = path.trimmed();
        if (isOp2Path(modelPath)) {
            resultPath = modelPath;
            resultPathAutoFilled = true;
        } else if (resultPathAutoFilled) {
            resultPath.clear();
            resultPathAutoFilled = false;
        }
    }

    void selectResultFile(const QString& path)
    {
        resultPath = path.trimmed();
        resultPathAutoFilled = false;
    }
};
