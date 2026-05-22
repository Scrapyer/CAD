#include "MacOSMetalLayerHost.h"

#include <QWindow>

#include <algorithm>
#include <cmath>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace {

CAMetalLayer* metalLayerFromWindow(QWindow* window, QString& error)
{
    if (!window) {
        error = QStringLiteral("QWindow is null");
        return nil;
    }
    if (!window->handle()) {
        window->create();
    }

    WId nativeId = window->winId();
    if (!nativeId) {
        error = QStringLiteral("QWindow has no native view");
        return nil;
    }

    NSView* nativeView = reinterpret_cast<NSView*>(nativeId);
    if (![nativeView isKindOfClass:[NSView class]]) {
        error = QStringLiteral("QWindow native handle is not an NSView");
        return nil;
    }

    [nativeView setWantsLayer:YES];
    CALayer* layer = [nativeView layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.frame = nativeView.bounds;
        NSScreen* screen = nativeView.window.screen ?: [NSScreen mainScreen];
        metalLayer.contentsScale = screen ? screen.backingScaleFactor : 1.0;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.framebufferOnly = YES;
        [nativeView setLayer:metalLayer];
        layer = metalLayer;
    }

    CAMetalLayer* metalLayer = static_cast<CAMetalLayer*>(layer);
    metalLayer.frame = nativeView.bounds;
    return metalLayer;
}

} // namespace

bool MacOSMetalLayerHost::prepare(QWindow* window, void* device, QString& lastError)
{
    return prepareMacOSMetalLayerForWindow(window,
                                          device,
                                          metalLayer_,
                                          drawableSize_,
                                          lastError);
}

void MacOSMetalLayerHost::resize(int width, int height, double devicePixelRatio)
{
    if (!metalLayer_) {
        return;
    }

    resizeMacOSMetalLayerDrawable(metalLayer_,
                                  width,
                                  height,
                                  devicePixelRatio,
                                  drawableSize_);
}

bool prepareMacOSMetalLayerForWindow(QWindow* window,
                                     void* device,
                                     void*& metalLayer,
                                     QSize& drawableSize,
                                     QString& lastError)
{
    metalLayer = nullptr;
    if (!device) {
        lastError = QStringLiteral("Metal device is not initialized");
        return false;
    }

    @autoreleasepool {
        CAMetalLayer* layer = metalLayerFromWindow(window, lastError);
        if (!layer) {
            return false;
        }

        layer.device = static_cast<id<MTLDevice>>(device);
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        if ([layer respondsToSelector:@selector(setDisplaySyncEnabled:)]) {
            layer.displaySyncEnabled = NO;
        }
        if ([layer respondsToSelector:@selector(setMaximumDrawableCount:)]) {
            layer.maximumDrawableCount = 3;
        }
        resizeMacOSMetalLayerDrawable(layer,
                                      window ? window->width() : 1,
                                      window ? window->height() : 1,
                                      window ? window->devicePixelRatio() : 1.0,
                                      drawableSize);
        metalLayer = layer;
    }
    return true;
}

void resizeMacOSMetalLayerDrawable(void* metalLayer,
                                   int width,
                                   int height,
                                   double devicePixelRatio,
                                   QSize& drawableSize)
{
    const double scale = std::max(devicePixelRatio, 1.0);
    const int drawableWidth = std::max(1, static_cast<int>(std::ceil(width * scale)));
    const int drawableHeight = std::max(1, static_cast<int>(std::ceil(height * scale)));
    drawableSize = QSize(drawableWidth, drawableHeight);

    @autoreleasepool {
        CAMetalLayer* layer = static_cast<CAMetalLayer*>(metalLayer);
        layer.contentsScale = static_cast<CGFloat>(scale);
        layer.drawableSize = CGSizeMake(drawableWidth, drawableHeight);
    }
}
