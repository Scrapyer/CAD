/**
 * @file FEAnimationController.cpp
 * @brief 帧动画控制器实现
 */

#include "FEAnimationController.h"

FEAnimationController::FEAnimationController(QObject* parent)
    : QObject(parent)
{
    connect(&timer_, &QTimer::timeout, this, &FEAnimationController::onTimerTick);
}

void FEAnimationController::setFrameCount(int count)
{
    frameCount_ = count;
    if (currentFrame_ >= frameCount_)
        setCurrentFrame(0);
}

void FEAnimationController::setFps(double fps)
{
    fps_ = fps;
    if (playing_)
        timer_.setInterval(static_cast<int>(1000.0 / fps_));
}

void FEAnimationController::play()
{
    if (frameCount_ < 2) return;
    playing_ = true;
    timer_.setInterval(static_cast<int>(1000.0 / fps_));
    timer_.start();
    emit playStateChanged(true);
}

void FEAnimationController::pause()
{
    playing_ = false;
    timer_.stop();
    emit playStateChanged(false);
}

void FEAnimationController::stop()
{
    playing_ = false;
    timer_.stop();
    setCurrentFrame(0);
    emit playStateChanged(false);
}

void FEAnimationController::setCurrentFrame(int frame)
{
    if (frameCount_ <= 0) frame = 0;
    else frame = frame % frameCount_;

    if (frame != currentFrame_) {
        currentFrame_ = frame;
        emit frameChanged(currentFrame_);
    }
}

void FEAnimationController::onTimerTick()
{
    setCurrentFrame(currentFrame_ + 1);
}
