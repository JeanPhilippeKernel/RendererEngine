// macOS native file-open dialog using NSOpenPanel.
// Must be compiled as Objective-C++ (.mm) because it uses AppKit.
// Called from C++ via the ZEngineOpenFileDialogAsync extern "C" entry point.
//
// Uses beginWithCompletionHandler, not runModal: runModal blocks the calling
// thread (the main thread, via MainThreadScheduler) until the panel closes,
// freezing the whole engine loop for as long as the dialog is open.
// beginWithCompletionHandler returns immediately; the completion handler
// fires later, asynchronously, on the main run loop.
#import <AppKit/AppKit.h>
#include <string>
#include <string_view>
#include <vector>

extern "C" void ZEngineOpenFileDialogAsync(const char** extensions, int count, const char* default_dir, const char* message, void* user_ctx, void (*on_complete)(void* user_ctx, const char* path))
{
    // Built up front so the block below captures safe, retained Objective-C
    // objects rather than raw C pointers that could dangle on the async path.
    NSMutableArray<NSString*>* types = nil;
    if (count > 0)
    {
        types = [NSMutableArray array];
        for (int i = 0; i < count; ++i)
        {
            std::string ext(extensions[i]);
            if (!ext.empty() && ext[0] == '.')
                ext = ext.substr(1);
            [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
        }
    }

    NSURL* dir_url = nil;
    if (default_dir && default_dir[0] != '\0')
        dir_url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:default_dir] isDirectory:YES];

    NSString* msg = [NSString stringWithUTF8String:(message && message[0] != '\0') ? message : "Select a file"];

    void (^run_panel)(void) = ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setMessage:msg];

        if (dir_url)
            [panel setDirectoryURL:dir_url];

        if (types)
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [panel setAllowedFileTypes:types];
#pragma clang diagnostic pop
        }

        [panel beginWithCompletionHandler:^(NSModalResponse result) {
            std::string path;
            if (result == NSModalResponseOK)
            {
                NSURL* url = [panel URL];
                if (url)
                    path = [[url path] UTF8String];
            }
            on_complete(user_ctx, path.c_str());
        }];
    };

    if ([NSThread isMainThread])
        run_panel();
    else
        dispatch_async(dispatch_get_main_queue(), run_panel);
}
