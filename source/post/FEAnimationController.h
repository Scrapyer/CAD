/**
 * @file FEAnimationController.h
 * @brief 帧动画控制器
 *
 * 用 QTimer 驱动帧索引循环播放，不直接操作渲染或结果数据。
 * 外部通过 frameChanged 信号获取当前帧索引。
 */

#pragma once

#include "ferender_export.h"

#include <QObject>
#include <QTimer>

class FERENDER_EXPORT FEAnimationController : public QObject {
    Q_OBJECT

public:
    explicit FEAnimationController(QObject* parent = nullptr);

    void setFrameCount(int count);
    int frameCount() const { return frameCount_; }

    void setFps(double fps);
    double fps() const { return fps_; }

    int currentFrame() const { return currentFrame_; }
    bool isPlaying() const { return playing_; }

public slots:
    void play();
    void pause();
    void stop();
    void setCurrentFrame(int frame);

signals:
    void frameChanged(int frameIndex);
    void playStateChanged(bool playing);

private slots:
    void onTimerTick();

private:
    QTimer timer_;
    int frameCount_ = 0;
    int currentFrame_ = 0;
    double fps_ = 12.0;
    bool playing_ = false;
};
