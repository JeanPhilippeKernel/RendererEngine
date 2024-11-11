using Avalonia.Controls;
using Avalonia.Platform.Storage;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    internal class StorageProviderService : IStorageProviderService
    {
        private Window _window;
        public StorageProviderService(Window window) => _window = window;

        public Task<DirectoryInfo> CreateDirectoryAsync(string path) => Task.Run(() => Directory.CreateDirectory(path));

        public Task<(bool, FileStream?)> CreateFileAsync(string path, bool overrideContent)
        {
            return Task.Run<(bool, FileStream?)>(() =>
            {
                bool exist = File.Exists(path);
                if (exist && overrideContent)
                {
                    return (true, File.Create(path));
                }
                else if (!(exist))
                {
                    return (true, File.Create(path));
                }
                else
                {
                    return (false, null);
                }
            });
        }

        public Task<bool> IsDirectoryExists(string path) => Task.Run(() => Directory.Exists(path));

        public Task<bool> IsFileExists(string path) => Task.Run(() => File.Exists(path));

        public async Task<IStorageFolder?> PickDirectoryAsync()
        {
            IReadOnlyList<IStorageFolder> folders = await _window.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { AllowMultiple = false });
            return folders.FirstOrDefault();
        }
    }
}
