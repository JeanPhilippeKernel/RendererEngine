#import <AppKit/AppKit.h>
#include <ZEngine/Windows/Platform/MacOSFileDialog.h>
#include <gtest/gtest.h>
#import <objc/runtime.h>
#include <string>
#include <thread>

// These stand-ins observe the real bridge without opening an OS picker or
// activating a window. They do not establish physical AppKit focus acceptance.
static uint32_t             DestroyedDialogOwners = 0;

@interface                  ZEngineDialogOwnerProbe : NSObject
@property bool              Visible;
@property bool              Miniaturized;
@property bool              Key;
@property uint32_t          FocusRequests;
@property(assign) NSWindow* Sheet;
@end
@implementation ZEngineDialogOwnerProbe
- (BOOL)isVisible
{
    return self.Visible;
}
- (NSWindow*)attachedSheet
{
    return self.Sheet;
}
- (BOOL)isMiniaturized
{
    return self.Miniaturized;
}
- (BOOL)isKeyWindow
{
    return self.Key;
}
- (void)makeKeyWindow
{
    ++self.FocusRequests;
}
- (void)dealloc
{
    ++DestroyedDialogOwners;
    [super dealloc];
}
@end

@interface                  ZEngineDialogApplicationProbe : NSObject
@property bool              Active;
@property(assign) NSWindow* Key;
@end
@implementation ZEngineDialogApplicationProbe
- (BOOL)isActive
{
    return self.Active;
}
- (NSWindow*)keyWindow
{
    return self.Key;
}
@end

@interface                          ZEngineDialogPanelProbe : NSObject
@property(assign) NSWindow*         Parent;
@property bool                      Standalone;
@property bool                      Save;
@property bool                      CompletedOnMain;
@property(copy) NSString*           Name;
@property(copy) NSArray<NSString*>* Types;
@property(copy) NSURL*              Directory;
@property(copy) NSString*           Message;
@property(copy) NSURL*              URL;
@property NSModalResponse           Response;
@property(copy) void (^BeforeCompletion)(void);
@end
@implementation ZEngineDialogPanelProbe
- (void)setAllowsMultipleSelection:(BOOL)value
{
    EXPECT_FALSE(value);
}
- (void)setCanChooseFiles:(BOOL)value
{
    EXPECT_TRUE(value);
}
- (void)setCanChooseDirectories:(BOOL)value
{
    EXPECT_FALSE(value);
}
- (void)setDirectoryURL:(NSURL*)value
{
    self.Directory = value;
}
- (void)setAllowedFileTypes:(NSArray<NSString*>*)value
{
    self.Types = value;
}
- (void)setNameFieldStringValue:(NSString*)value
{
    EXPECT_NE(self.Types, nil);
    self.Name = value;
}
- (void)beginSheetModalForWindow:(NSWindow*)parent completionHandler:(void (^)(NSModalResponse))complete
{
    self.Parent          = parent;
    self.CompletedOnMain = [NSThread isMainThread];
    if (self.BeforeCompletion)
        self.BeforeCompletion();
    complete(self.Response);
}
- (void)beginWithCompletionHandler:(void (^)(NSModalResponse))complete
{
    self.Standalone = true;
    complete(self.Response);
}
- (void)dealloc
{
    [_Name release];
    [_Types release];
    [_Directory release];
    [_Message release];
    [_URL release];
    [_BeforeCompletion release];
    [super dealloc];
}
@end

namespace
{
    ZEngineDialogPanelProbe* ActivePanel  = nil;
    uint32_t                 FactoryCalls = 0;

    id                       OpenFactory(id, SEL)
    {
        ++FactoryCalls;
        ActivePanel.Save = false;
        return ActivePanel;
    }

    id SaveFactory(id, SEL)
    {
        ++FactoryCalls;
        ActivePanel.Save = true;
        return ActivePanel;
    }

    struct DialogCompletion
    {
        uint32_t    Count  = 0;
        bool        Failed = false;
        bool        Main   = false;
        std::string Path   = {};

        static void Complete(void* context, const char* path, bool failed)
        {
            auto& value = *static_cast<DialogCompletion*>(context);
            ++value.Count;
            value.Failed = failed;
            value.Main   = [NSThread isMainThread];
            value.Path   = path;
        }
    };
} // namespace

class MacOSFileDialogTest : public testing::Test
{
protected:
    NSAutoreleasePool*             Pool         = nil;
    NSApplication*                 PreviousApp  = nil;
    ZEngineDialogApplicationProbe* Application  = nil;
    ZEngineDialogOwnerProbe*       Owner        = nil;
    ZEngineDialogPanelProbe*       Panel        = nil;
    Method                         OpenMethod   = nullptr;
    Method                         SaveMethod   = nullptr;
    IMP                            PreviousOpen = nullptr;
    IMP                            PreviousSave = nullptr;
    DialogCompletion               Completion   = {};

    void                           SetUp() override
    {
        ASSERT_TRUE([NSThread isMainThread]);
        Pool                  = [[NSAutoreleasePool alloc] init];
        PreviousApp           = NSApp;
        Application           = [[ZEngineDialogApplicationProbe alloc] init];
        Application.Active    = true;
        NSApp                 = reinterpret_cast<NSApplication*>(Application);
        Owner                 = [[ZEngineDialogOwnerProbe alloc] init];
        Owner.Visible         = true;
        Panel                 = [[ZEngineDialogPanelProbe alloc] init];
        Panel.Response        = NSModalResponseCancel;
        ActivePanel           = Panel;
        FactoryCalls          = 0;
        DestroyedDialogOwners = 0;
        OpenMethod            = class_getClassMethod([NSOpenPanel class], @selector(openPanel));
        SaveMethod            = class_getClassMethod([NSSavePanel class], @selector(savePanel));
        ASSERT_NE(OpenMethod, nullptr);
        ASSERT_NE(SaveMethod, nullptr);
        PreviousOpen = method_setImplementation(OpenMethod, reinterpret_cast<IMP>(&OpenFactory));
        PreviousSave = method_setImplementation(SaveMethod, reinterpret_cast<IMP>(&SaveFactory));
    }

    void TearDown() override
    {
        DrainMainQueue();
        if (PreviousOpen)
            method_setImplementation(OpenMethod, PreviousOpen);
        if (PreviousSave)
            method_setImplementation(SaveMethod, PreviousSave);
        ActivePanel = nil;
        [Panel release];
        [Owner release];
        NSApp = PreviousApp;
        [Application release];
        [Pool drain];
    }

    void DrainMainQueue()
    {
        __block bool done = false;
        dispatch_async(dispatch_get_main_queue(), ^{ done = true; });
        for (uint32_t i = 0; i < 100 && !done; ++i)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
        ASSERT_TRUE(done);
    }

    void Show(bool save = false)
    {
        const char* filters[] = {".zescene", ".glb"};
        ZEngineDesktopFileDialogAsync(Owner, save, filters, 2, "/tmp/Scene \u00e9", "Example.zescene", "Select scene", &Completion, &DialogCompletion::Complete);
    }
};

TEST_F(MacOSFileDialogTest, OpenAndSaveUseSheetsForTheExactOwnerAndKeepFilterOrder)
{
    for (const bool save : {false, true})
    {
        Completion = {};
        Show(save);
        EXPECT_EQ(Panel.Parent, reinterpret_cast<NSWindow*>(Owner));
        EXPECT_FALSE(Panel.Standalone);
        EXPECT_EQ(Panel.Save, save);
        EXPECT_TRUE(Panel.CompletedOnMain);
        EXPECT_TRUE(([Panel.Types isEqualToArray:@[ @"zescene", @"glb" ]]));
        EXPECT_TRUE([[[Panel.Directory path] precomposedStringWithCanonicalMapping] isEqualToString:@"/tmp/Scene \u00e9"]);
        EXPECT_TRUE([Panel.Directory hasDirectoryPath]);
        EXPECT_TRUE([Panel.Message isEqualToString:@"Select scene"]);
        if (save)
            EXPECT_TRUE([Panel.Name isEqualToString:@"Example.zescene"]);
        EXPECT_EQ(Completion.Count, 1u);
        EXPECT_FALSE(Completion.Failed);
        EXPECT_TRUE(Completion.Main);
        EXPECT_TRUE(Completion.Path.empty());
    }
    EXPECT_EQ(FactoryCalls, 2u);
}

TEST_F(MacOSFileDialogTest, SelectedUnicodePathAndMissingSelectedURLRemainDistinctFromCancel)
{
    Panel.Response = NSModalResponseOK;
    Panel.URL      = [NSURL fileURLWithPath:@"/tmp/Scene \u00e9/\u5834\u9762.zescene"];
    Show();
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_FALSE(Completion.Failed);
    // NSURL can normalize a filesystem path to decomposed Unicode. Preserve its
    // exact native UTF-8 while checking the intended canonical spelling too.
    EXPECT_EQ(Completion.Path, std::string([[Panel.URL path] UTF8String]));
    EXPECT_TRUE([[[NSString stringWithUTF8String:Completion.Path.c_str()] precomposedStringWithCanonicalMapping] isEqualToString:@"/tmp/Scene \u00e9/\u5834\u9762.zescene"]);
    Completion = {};
    Panel.URL  = nil;
    Show();
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_TRUE(Completion.Path.empty());
}

TEST_F(MacOSFileDialogTest, HiddenOwnerAndExistingSheetFailWithoutCreatingAnUnownedPicker)
{
    Owner.Visible = false;
    Show();
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    Completion    = {};
    Owner.Visible = true;
    Owner.Sheet   = reinterpret_cast<NSWindow*>(Panel);
    Show(true);
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_EQ(FactoryCalls, 0u);
}

TEST_F(MacOSFileDialogTest, InvalidUTF8NeverReachesAppKitPanelConfiguration)
{
    const char  invalid[] = {char(0xc0), char(0xaf), 0};
    const char* filters[] = {".zescene"};
    for (uint32_t i = 0; i < 4; ++i)
    {
        Completion             = {};
        const char* selected[] = {i == 0 ? invalid : filters[0]};
        ZEngineDesktopFileDialogAsync(Owner, true, selected, 1, i == 1 ? invalid : "/tmp", i == 2 ? invalid : "Example.zescene", i == 3 ? invalid : "Select scene", &Completion, &DialogCompletion::Complete);
        EXPECT_EQ(Completion.Count, 1u);
        EXPECT_TRUE(Completion.Failed);
        EXPECT_TRUE(Completion.Path.empty());
    }
    EXPECT_EQ(FactoryCalls, 0u);
}

TEST_F(MacOSFileDialogTest, MissingOwnerInvalidFilterBoundsAndEmptyExtensionsFailOnce)
{
    const char* filters[] = {".zescene"};
    for (const int count : {-1, 65})
    {
        Completion = {};
        ZEngineDesktopFileDialogAsync(Owner, false, filters, count, "", "", "", &Completion, &DialogCompletion::Complete);
        EXPECT_EQ(Completion.Count, 1u);
        EXPECT_TRUE(Completion.Failed);
    }
    for (const char* extension : {static_cast<const char*>(nullptr), "", "."})
    {
        Completion            = {};
        const char* invalid[] = {extension};
        ZEngineDesktopFileDialogAsync(Owner, false, invalid, 1, "", "", "", &Completion, &DialogCompletion::Complete);
        EXPECT_EQ(Completion.Count, 1u);
        EXPECT_TRUE(Completion.Failed);
    }
    Completion = {};
    ZEngineDesktopFileDialogAsync(nullptr, false, filters, 1, "", "", "", &Completion, &DialogCompletion::Complete);
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    Completion = {};
    ZEngineDesktopFileDialogAsync(Owner, false, nullptr, 1, "", "", "", &Completion, &DialogCompletion::Complete);
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_EQ(FactoryCalls, 0u);
}

TEST_F(MacOSFileDialogTest, EmptyOptionalValuesUseSystemDefaultsWithoutInventingFilters)
{
    ZEngineDesktopFileDialogAsync(Owner, false, nullptr, 0, nullptr, nullptr, nullptr, &Completion, &DialogCompletion::Complete);
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_FALSE(Completion.Failed);
    EXPECT_TRUE(Completion.Path.empty());
    EXPECT_EQ(Panel.Types, nil);
    EXPECT_EQ(Panel.Directory, nil);
    EXPECT_TRUE([Panel.Message isEqualToString:@"Select a file"]);
    EXPECT_EQ(Panel.Parent, reinterpret_cast<NSWindow*>(Owner));
}

TEST_F(MacOSFileDialogTest, NativePanelCreationFailureCompletesOnceAndMissingCallbackDoesNothing)
{
    ActivePanel = nil;
    Show();
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_EQ(FactoryCalls, 1u);
    ZEngineDesktopFileDialogAsync(Owner, false, nullptr, 0, nullptr, nullptr, nullptr, &Completion, nullptr);
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_EQ(FactoryCalls, 1u);
}

TEST_F(MacOSFileDialogTest, QueuedWorkerPresentationChecksOwnerOnTheMainLoopAndRetainsCopiedStrings)
{
    std::thread worker([&] { Show(true); });
    worker.join();
    EXPECT_EQ(Completion.Count, 0u);
    EXPECT_EQ(FactoryCalls, 0u);
    // The caller can lose its visible owner before queued presentation runs.
    Owner.Visible = false;
    for (uint32_t i = 0; i < 100 && !Completion.Count; ++i)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_TRUE(Completion.Main);
    EXPECT_EQ(FactoryCalls, 0u);
    Owner.Visible = true;
    Completion    = {};
    std::thread visible([&] { Show(true); });
    visible.join();
    for (uint32_t i = 0; i < 100 && !Completion.Count; ++i)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_FALSE(Completion.Failed);
    EXPECT_TRUE(Completion.Main);
    EXPECT_EQ(Panel.Parent, reinterpret_cast<NSWindow*>(Owner));
    EXPECT_TRUE([Panel.Name isEqualToString:@"Example.zescene"]);
}

TEST_F(MacOSFileDialogTest, QueuedPresentationRetainsTheOwnerUntilItsMainLoopRejection)
{
    Owner.Visible = false;
    std::thread worker([&] { Show(true); });
    worker.join();
    [Owner release];
    Owner = nil;
    EXPECT_EQ(DestroyedDialogOwners, 0u);
    for (uint32_t i = 0; i < 100 && !Completion.Count; ++i)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    EXPECT_EQ(Completion.Count, 1u);
    EXPECT_TRUE(Completion.Failed);
    EXPECT_TRUE(Completion.Main);
    EXPECT_EQ(FactoryCalls, 0u);
    EXPECT_EQ(DestroyedDialogOwners, 1u);
}

TEST_F(MacOSFileDialogTest, DismissalRestoresTheOriginallyKeyOwnerOnlyAfterAppKitReleasesTheSheet)
{
    for (const bool save : {false, true})
    {
        for (const NSModalResponse response : {NSModalResponseCancel, NSModalResponseOK})
        {
            SCOPED_TRACE(save);
            SCOPED_TRACE(response);
            Completion             = {};
            Owner.Key              = true;
            Owner.FocusRequests    = 0;
            Panel.Response         = response;
            Panel.URL              = [NSURL fileURLWithPath:@"/tmp/Selected.zescene"];
            // AppKit still owns the key window and attached sheet while invoking
            // the completion. Focus can only be restored after it unwinds.
            Panel.BeforeCompletion = ^{
              Owner.Key       = false;
              Owner.Sheet     = reinterpret_cast<NSWindow*>(Panel);
              Application.Key = reinterpret_cast<NSWindow*>(Panel);
            };
            Show(save);
            EXPECT_EQ(Completion.Count, 1u);
            EXPECT_FALSE(Completion.Failed);
            EXPECT_EQ(Owner.FocusRequests, 0u);
            Owner.Sheet     = nil;
            Application.Key = nil;
            DrainMainQueue();
            EXPECT_EQ(Owner.FocusRequests, 1u);
            EXPECT_EQ(Completion.Count, 1u);
        }
    }
}

TEST_F(MacOSFileDialogTest, DeferredFocusNeverStealsAnotherWindowOrResurrectsAnUnavailableOwner)
{
    for (uint32_t state = 0; state < 7; ++state)
    {
        SCOPED_TRACE(state);
        Completion             = {};
        Owner.Visible          = true;
        Owner.Miniaturized     = false;
        Owner.Sheet            = nil;
        Owner.Key              = state != 0;
        Owner.FocusRequests    = 0;
        Application.Active     = true;
        Application.Key        = nil;
        Panel.BeforeCompletion = nil;
        Show(state % 2 != 0);
        ASSERT_EQ(Completion.Count, 1u);
        EXPECT_FALSE(Completion.Failed);
        EXPECT_EQ(Owner.FocusRequests, 0u);
        if (state == 1)
            Application.Active = false;
        else if (state == 2)
            Application.Key = reinterpret_cast<NSWindow*>(Panel);
        else if (state == 3)
            Owner.Visible = false;
        else if (state == 4)
            Owner.Miniaturized = true;
        else if (state == 5)
            Owner.Sheet = reinterpret_cast<NSWindow*>(Panel);
        else if (state == 6)
            Application.Key = reinterpret_cast<NSWindow*>(Owner);
        DrainMainQueue();
        EXPECT_EQ(Owner.FocusRequests, 0u);
        EXPECT_EQ(Completion.Count, 1u);
    }
}

TEST_F(MacOSFileDialogTest, DeferredRestorationRetainsTheOwnerUntilItsCheckedMainLoopDelivery)
{
    Owner.Key = true;
    Show();
    ASSERT_EQ(Completion.Count, 1u);
    EXPECT_EQ(Owner.FocusRequests, 0u);
    Application.Active = false;
    [Owner release];
    Owner = nil;
    EXPECT_EQ(DestroyedDialogOwners, 0u);
    DrainMainQueue();
    EXPECT_EQ(DestroyedDialogOwners, 1u);
    EXPECT_EQ(Completion.Count, 1u);
}
