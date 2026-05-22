#include "MetalLayerHost.h"

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

bool prepareMetalLayerForWindow(QWindow* window,
                                void* device,
                                void*& metalLayer,
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
        metalLayer = layer;
    }
    return true;
}

void resizeMetalLayerDrawable(void* metalLayer,
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
