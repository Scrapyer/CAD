#pragma once

#include <QSize>
#include <QString>

class QWindow;

class MacOSMetalLayerHost {
public:
    MacOSMetalLayerHost() = default;

    bool prepare(QWindow* window, void* device, QString& lastError);
    void resize(int width, int height, double devicePixelRatio);

    bool hasLayer() const { return metalLayer_ != nullptr; }
    void* layer() const { return metalLayer_; }
    const QSize& drawableSize() const { return drawableSize_; }

private:
    void* metalLayer_ = nullptr;
    QSize drawableSize_;
};

bool prepareMacOSMetalLayerForWindow(QWindow* window,
                                     void* device,
                                     void*& metalLayer,
                                     QSize& drawableSize,
                                     QString& lastError);

void resizeMacOSMetalLayerDrawable(void* metalLayer,
                                   int width,
                                   int height,
                                   double devicePixelRatio,
                                   QSize& drawableSize);
