// macOS native open/save dialogs using NSOpenPanel / NSSavePanel.
// Must be compiled as Objective-C++ (.mm) because it uses AppKit.
// Called from C++ through the checked desktop-picker adapter.
//
// Uses an owner-window sheet, not runModal: runModal blocks the calling
// thread (the main thread, via MainThreadScheduler) until the panel closes,
// freezing the whole engine loop for as long as the dialog is open.
// The sheet returns immediately; the completion handler
// fires later, asynchronously, on the main run loop.
#import <AppKit/AppKit.h>
#include <ZEngine/Windows/Platform/MacOSFileDialog.h>
#include <string>

extern "C" void ZEngineDesktopFileDialogAsync(void* parent_window, bool save, const char** extensions, int count, const char* default_dir, const char* filename, const char* message, void* user_ctx, void (*on_complete)(void* user_ctx, const char* path, bool failed))
{
    @autoreleasepool
    {
        if (!on_complete)
            return;
        if (!parent_window || count < 0 || count > 64 || (count && !extensions))
        {
            on_complete(user_ctx, "", true);
            return;
        }
        NSWindow*                  parent = static_cast<NSWindow*>(parent_window);
        // Built up front so the block below captures safe, retained Objective-C
        // objects rather than raw C pointers that could dangle on the async path.
        NSMutableArray<NSString*>* types  = nil;
        if (count > 0)
        {
            types = [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(count)];
            for (int i = 0; i < count; ++i)
            {
                const char* extension = extensions[i];
                if (extension && extension[0] == '.')
                    ++extension;
                NSString* type = extension ? [NSString stringWithUTF8String:extension] : nil;
                if (!type || ![type length])
                {
                    on_complete(user_ctx, "", true);
                    return;
                }
                [types addObject:type];
            }
        }

        NSURL* dir_url = nil;
        if (default_dir && default_dir[0] != '\0')
        {
            NSString* directory = [NSString stringWithUTF8String:default_dir];
            if (!directory)
            {
                on_complete(user_ctx, "", true);
                return;
            }
            dir_url = [NSURL fileURLWithPath:directory isDirectory:YES];
        }

        NSString* msg  = [NSString stringWithUTF8String:(message && message[0] != '\0') ? message : "Select a file"];
        NSString* name = [NSString stringWithUTF8String:filename ? filename : ""];
        if (!msg || !name)
        {
            on_complete(user_ctx, "", true);
            return;
        }

        void (^run_panel)(void) = ^{
          // Do not create an unowned picker, queue behind another modal sheet or
          // resurrect an owner hidden/closed before this main-loop dispatch.
          // Never activate another application or guess the owner from keyWindow
          // after an asynchronous completion.
          if (![parent isVisible] || [parent attachedSheet])
          {
              on_complete(user_ctx, "", true);
              return;
          }
          NSSavePanel* panel = nil;
          if (save)
          {
              panel = [NSSavePanel savePanel];
          }
          else
          {
              NSOpenPanel* open = [NSOpenPanel openPanel];
              [open setAllowsMultipleSelection:NO];
              [open setCanChooseFiles:YES];
              [open setCanChooseDirectories:NO];
              panel = open;
          }
          if (!panel)
          {
              on_complete(user_ctx, "", true);
              return;
          }
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

          // Configure the permitted extension first. AppKit can append it again
          // when the suggested name already contains it before the filter is set.
          if (save)
              [panel setNameFieldStringValue:name];

          const bool restore_focus = [parent isKeyWindow];
          [panel beginSheetModalForWindow:parent
                        completionHandler:^(NSModalResponse result) {
                          // AppKit invokes completion before detaching the sheet.
                          // GLFW can otherwise be left without a key window. Wait
                          // until dismissal unwinds, and never steal new focus.
                          if (restore_focus)
                              dispatch_async(dispatch_get_main_queue(), ^{
                                if ([NSApp isActive] && [parent isVisible] && ![parent isMiniaturized] && ![parent attachedSheet] && ![NSApp keyWindow])
                                    [parent makeKeyWindow];
                              });
                          std::string path = {};
                          if (result == NSModalResponseOK)
                          {
                              NSURL*      url      = [panel URL];
                              const char* selected = url ? [[url path] UTF8String] : nullptr;
                              if (selected)
                                  path = selected;
                          }
                          on_complete(user_ctx, path.c_str(), result == NSModalResponseOK && path.empty());
                        }];
        };

        if ([NSThread isMainThread])
            run_panel();
        else
            dispatch_async(dispatch_get_main_queue(), run_panel);
    }
}
