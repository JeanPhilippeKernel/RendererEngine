#import <AppKit/AppKit.h>

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
