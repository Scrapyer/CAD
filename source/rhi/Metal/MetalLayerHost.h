#pragma once

#include <QSize>
#include <QString>

class QWindow;

bool prepareMetalLayerForWindow(QWindow* window,
                                void* device,
                                void*& metalLayer,
                                QString& lastError);

void resizeMetalLayerDrawable(void* metalLayer,
                              int width,
                              int height,
                              double devicePixelRatio,
                              QSize& drawableSize);
