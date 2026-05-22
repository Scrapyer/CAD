#pragma once

#include <QVector3D>

#include <cstdint>

class QString;

QVector3D metalIdToPickColor(int id);
int metalPickColorToId(const uint8_t* pixel);
void clearMetalReadbackPixel(void* readbackBuffer);
bool readMetalPickElementId(void* readbackBuffer, int& elementId, QString& lastError);
