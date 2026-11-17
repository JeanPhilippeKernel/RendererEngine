#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" void ZEngineSetDockIcon(const char* path)
{
    NSString* ns_path = [NSString stringWithUTF8String:path];
    NSImage*  image   = [[NSImage alloc] initWithContentsOfFile:ns_path];
    if (image)
    {
        [[NSApplication sharedApplication] setApplicationIconImage:image];
    }
}

extern "C" int ZEngineWindowOcclusionVisible(void* window)
{
    if (!window)
        return -1;
    NSWindow* native_window = (__bridge NSWindow*) window;
    return (native_window.occlusionState & NSWindowOcclusionStateVisible) != 0 ? 1 : 0;
}

extern "C" int ZEngineConfigureMetalLayerDelegate(void* window)
{
    if (!window)
        return 0;
    NSWindow* native_window = (__bridge NSWindow*) window;
    NSView* view = native_window.contentView;
    CALayer* layer = view.layer;
    if (![layer isKindOfClass:[CAMetalLayer class]])
        return 0;
    if (!layer.delegate)
    {
        layer.delegate = (id<CALayerDelegate>) view;
        return 1;
    }
    return layer.delegate == (id<CALayerDelegate>) view ? 2 : 3;
}
