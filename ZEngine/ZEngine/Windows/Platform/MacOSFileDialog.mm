// macOS native file-open dialog using NSOpenPanel.
// Must be compiled as Objective-C++ (.mm) because it uses AppKit.
// Called from C++ via the ZEngineOpenFileDialog extern "C" entry point.
#import <AppKit/AppKit.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

extern "C" std::string ZEngineOpenFileDialog(const char** extensions, int count)
{
    __block std::string result;

    // NSOpenPanel must run on the main thread.
    void (^run_panel)(void) = ^{
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setAllowsMultipleSelection:NO];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setMessage:@"Select a 3D asset file"];

        if (count > 0)
        {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (int i = 0; i < count; ++i)
            {
                std::string ext(extensions[i]);
                if (!ext.empty() && ext[0] == '.')
                    ext = ext.substr(1);
                [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
            }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [panel setAllowedFileTypes:types];
#pragma clang diagnostic pop
        }

        if ([panel runModal] == NSModalResponseOK)
        {
            NSURL* url = [panel URL];
            if (url)
                result = [[url path] UTF8String];
        }
    };

    if ([NSThread isMainThread])
        run_panel();
    else
        dispatch_sync(dispatch_get_main_queue(), run_panel);

    return result;
}
