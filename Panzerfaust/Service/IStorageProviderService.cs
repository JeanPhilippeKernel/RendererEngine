using Avalonia.Platform.Storage;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    internal interface IStorageProviderService
    {
        Task<IStorageFolder?> PickDirectoryAsync();
        Task<DirectoryInfo> CreateDirectoryAsync(string path);
        Task<bool> IsDirectoryExists(string path);
        Task<bool> IsFileExists(string path);

        Task<(bool, FileStream?)> CreateFileAsync(string path, bool overrideContent = false);
    }
}
