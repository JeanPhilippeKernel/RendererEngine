#include <pch.h>
#include <WindowsHelper.h>
#include <ZEngine.h>

#ifdef _WIN32
#include <shobjidl.h>
#include <windows.h>
#endif

namespace Tetragrama::Helpers
{
    std::future<std::string> OpenFileDialogAsync()
    {
        std::string selected_filename;
#ifdef _WIN32
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr))
        {
            IFileOpenDialog* pFileOpen;

            // Create the FileOpenDialog object.
            hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

            if (SUCCEEDED(hr))
            {
                // Show the Open dialog box.
                hr = pFileOpen->Show(NULL);

                // Get the file name from the dialog box.
                if (SUCCEEDED(hr))
                {
                    IShellItem* pItem;
                    hr = pFileOpen->GetResult(&pItem);
                    if (SUCCEEDED(hr))
                    {
                        PWSTR pszFilePath;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                        // Display the file name to the user.
                        if (SUCCEEDED(hr))
                        {
                            std::wstring wstring(pszFilePath);
                            selected_filename = std::string(std::begin(wstring), std::end(wstring));
                            CoTaskMemFree(pszFilePath);
                        }
                        pItem->Release();
                    }
                }
                pFileOpen->Release();
            }
        }
        CoUninitialize();
#endif
        co_return selected_filename;
    }
} // namespace Tetragrama::Helpers