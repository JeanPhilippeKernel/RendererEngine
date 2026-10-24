#pragma once

// Private C++ / Objective-C++ bridge. parent_window is the caller's live Cocoa
// NSWindow, never NSApp's current key window (which can be another dialog).
extern "C" void ZEngineDesktopFileDialogAsync(void* parent_window, bool save, const char** extensions, int count, const char* directory, const char* filename, const char* message, void* context, void (*complete)(void*, const char*, bool));
