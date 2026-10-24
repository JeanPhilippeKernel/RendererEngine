#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Windows/Platform/DesktopFileDialog.h>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <ShObjIdl.h>
#include <process.h>
#elif defined(__linux__)
#include <ZEngine/Windows/Platform/WaylandPortalFileDialog.h>
#include <pthread.h>
#elif defined(__APPLE__)
#include <ZEngine/Windows/Platform/MacOSFileDialog.h>
#endif

namespace ZEngine::Windows::Platform
{
    namespace
    {
#if defined(_WIN32)
        struct ComRelease
        {
            void operator()(IUnknown* object) const
            {
                if (object)
                    object->Release();
            }
        };

        bool ToWide(std::string_view text, std::wstring* out)
        {
            if (text.size() > INT_MAX || text.find('\0') != std::string_view::npos)
                return false;
            if (text.empty())
            {
                out->clear();
                return true;
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0)
                return false;
            out->resize(static_cast<size_t>(count));
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out->data(), count) == count;
        }
#endif

#if defined(_WIN32) || defined(__linux__)
        struct DialogWork
        {
            FileDialogRequest        Request   = {};
            std::vector<std::string> Filters   = {};
            std::string              Directory = {};
            std::string              Filename  = {};
            std::string              Title     = {};
            bool                     Save      = false;
#if defined(_WIN32)
            HWND Parent = nullptr;

            bool Run()
            {
                struct Apartment
                {
                    HRESULT Status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                    ~Apartment()
                    {
                        if (SUCCEEDED(Status))
                            CoUninitialize();
                    }
                } apartment = {};
                if (FAILED(apartment.Status))
                    return false;
                IFileDialog* raw = nullptr;
                if (FAILED(CoCreateInstance(Save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileDialog, reinterpret_cast<void**>(&raw))))
                    return false;
                const std::unique_ptr<IFileDialog, ComRelease> dialog(raw);
                FILEOPENDIALOGOPTIONS                          options = {};
                if (FAILED(dialog->GetOptions(&options)) || FAILED(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_STRICTFILETYPES | (Save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST))))
                    return false;
                std::wstring patterns, extension, directory, filename, title;
                for (const auto& filter : Filters)
                {
                    if (!ToWide(filter, &extension))
                        return false;
                    if (!patterns.empty())
                        patterns += L';';
                    patterns += L'*';
                    patterns += extension;
                }
                if (patterns.empty())
                    patterns = L"*.*";
                const COMDLG_FILTERSPEC filter = {L"Supported files", patterns.c_str()};
                if (FAILED(dialog->SetFileTypes(1, &filter)) || !ToWide(Filename, &filename) || !ToWide(Title, &title) || !ToWide(Directory, &directory))
                    return false;
                if (Save && !filename.empty() && FAILED(dialog->SetFileName(filename.c_str())))
                    return false;
                if (Save && !Filters.empty())
                {
                    const std::string_view suffix = Filters.front();
                    if (!ToWide(suffix.starts_with('.') ? suffix.substr(1) : suffix, &extension) || FAILED(dialog->SetDefaultExtension(extension.c_str())))
                        return false;
                }
                if (!title.empty() && FAILED(dialog->SetTitle(title.c_str())))
                    return false;
                if (!directory.empty())
                {
                    IShellItem* folder_raw = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&folder_raw))))
                    {
                        const std::unique_ptr<IShellItem, ComRelease> folder(folder_raw);
                        (void) dialog->SetFolder(folder.get());
                    }
                }
                const HRESULT shown = dialog->Show(Parent);
                if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
                    return Request.Complete({});
                if (FAILED(shown))
                    return false;
                IShellItem* selected_raw = nullptr;
                if (FAILED(dialog->GetResult(&selected_raw)))
                    return false;
                const std::unique_ptr<IShellItem, ComRelease> selected(selected_raw);
                PWSTR                                         path_raw = nullptr;
                if (FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path_raw)))
                    return false;
                const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(path_raw, &CoTaskMemFree);
                const int                                                count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.get(), -1, nullptr, 0, nullptr, nullptr);
                if (count <= 1)
                    return false;
                std::string utf8(static_cast<size_t>(count), '\0');
                if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.get(), -1, utf8.data(), count, nullptr, nullptr) != count)
                    return false;
                utf8.resize(static_cast<size_t>(count - 1));
                return Request.Complete(std::move(utf8));
            }
            static unsigned __stdcall Thread(void* context)
            {
                ZEngine::Helpers::Scope<DialogWork> work(static_cast<DialogWork*>(context));
                if (!work->Run())
                    work->Request.Fail();
                return 0;
            }
#else
            ZEngine::Helpers::Scope<WaylandPortalParent> WaylandParent = {};
            std::string                                  PortalParent  = {};
            unsigned long                                X11Parent     = 0;
            bool                                         UseX11        = false;

            void                                         Run()
            {
                auto result = OpenPortalFileDialog(PortalParent, Filters, Directory, Title, Save, Filename);
                if (result.Outcome == PortalFileDialogStatus::Failed)
                {
                    auto path = OpenLinuxFallbackFileDialog(X11Parent, UseX11, Filters, Directory, Title, Save, Filename);
                    // The legacy fallback only supplies a path; an empty result is
                    // cancellation (portal/native failures above remain explicit).
                    Request.Complete(std::move(path));
                }
                else if (result.Outcome == PortalFileDialogStatus::Cancelled)
                    Request.Complete({});
                else
                    Request.Complete(std::move(result.Path));
            }
            static void* Thread(void* context)
            {
                ZEngine::Helpers::Scope<DialogWork> work(static_cast<DialogWork*>(context));
                work->Run();
                return nullptr;
            }
#endif
        };
#endif
    } // namespace

    FileDialogRequest ShowDesktopFileDialog(GLFWwindow* window, bool save, std::span<std::string_view> filters, std::string_view directory, std::string_view filename, std::string_view title)
    {
        auto request = FileDialogRequest::Create();
        if (!window || filters.size() > 64 || directory.find('\0') != std::string_view::npos || filename.find('\0') != std::string_view::npos || title.find('\0') != std::string_view::npos)
        {
            request.Fail();
            return request;
        }
        for (const auto filter : filters)
            if (filter.empty() || filter.size() > 32 || filter.find_first_of(std::string_view{"\0;*/\\", 5}) != std::string_view::npos)
            {
                request.Fail();
                return request;
            }
#if defined(__APPLE__)
        std::vector<std::string> owned_filters  = {};
        std::vector<const char*> native_filters = {};
        owned_filters.reserve(filters.size());
        native_filters.reserve(filters.size());
        for (const auto filter : filters)
            owned_filters.emplace_back(filter);
        for (const auto& filter : owned_filters)
            native_filters.push_back(filter.c_str());
        auto* completion = new (std::nothrow) FileDialogRequest(request);
        if (!completion)
            request.Fail();
        else
        {
            ZEngineDesktopFileDialogAsync(glfwGetCocoaWindow(window), save, native_filters.data(), static_cast<int>(native_filters.size()), std::string(directory).c_str(), std::string(filename).c_str(), std::string(title).c_str(), completion, [](void* context, const char* path, bool failed) {
                ZEngine::Helpers::Scope<FileDialogRequest> result(static_cast<FileDialogRequest*>(context));
                if (failed)
                    result->Fail();
                else
                    result->Complete(path ? path : "");
            });
        }
#elif defined(_WIN32) || defined(__linux__)
        auto work = ZEngine::Helpers::Scope<DialogWork>(new (std::nothrow) DialogWork{});
        if (!work)
        {
            request.Fail();
            return request;
        }
        work->Request   = request;
        work->Save      = save;
        work->Directory = directory;
        work->Filename  = filename;
        work->Title     = title;
        for (const auto filter : filters)
            work->Filters.emplace_back(filter);
#if defined(_WIN32)
        work->Parent           = glfwGetWin32Window(window);
        const uintptr_t thread = _beginthreadex(nullptr, 0, &DialogWork::Thread, work.get(), 0, nullptr);
        if (thread)
        {
            work.release();
            CloseHandle(reinterpret_cast<HANDLE>(thread));
        }
        else
            request.Fail();
#else
        const int platform = glfwGetPlatform();
        work->UseX11       = platform == GLFW_PLATFORM_X11;
        if (platform == GLFW_PLATFORM_WAYLAND)
        {
            work->WaylandParent = CreateWaylandPortalParent(window);
            if (work->WaylandParent)
                work->PortalParent = work->WaylandParent->Handle();
        }
        else if (work->UseX11)
        {
            work->X11Parent = glfwGetX11Window(window);
            char parent[32] = {};
            std::snprintf(parent, sizeof(parent), "x11:0x%lx", work->X11Parent);
            work->PortalParent = parent;
        }
        pthread_attr_t attributes = {};
        if (pthread_attr_init(&attributes) != 0)
            request.Fail();
        else
        {
            pthread_t  thread  = {};
            const bool started = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) == 0 && pthread_create(&thread, &attributes, &DialogWork::Thread, work.get()) == 0;
            pthread_attr_destroy(&attributes);
            if (started)
                work.release();
            else
                request.Fail();
        }
#endif
#else
        request.Fail();
#endif
        return request;
    }
} // namespace ZEngine::Windows::Platform
