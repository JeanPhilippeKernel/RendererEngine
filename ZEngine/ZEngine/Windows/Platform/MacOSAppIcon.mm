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
