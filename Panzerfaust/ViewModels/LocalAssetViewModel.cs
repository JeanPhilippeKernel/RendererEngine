using ReactiveUI;
using System;
using System.IO;
using System.Reactive;
using System.Runtime.InteropServices;

namespace Panzerfaust.ViewModels
{
    internal class LocalAssetViewModel : ReactiveObject
    {
        public string AssetId { get; }
        public string FileName { get; }
        public string Resolution { get; }
        public string Format { get; }
        public string FilePath { get; }
        public long FileSizeBytes { get; }

        public string Label => $"{AssetId} · {Resolution} {Format.ToUpperInvariant()}";

        public static string ShowInFilesLabel =>
            RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "Show in Explorer" :
            RuntimeInformation.IsOSPlatform(OSPlatform.OSX)     ? "Show in Finder" :
                                                                   "Show in Files";
        public string SizeLabel => FileSizeBytes >= 1_048_576
            ? $"{FileSizeBytes / 1_048_576.0:F1} MB"
            : $"{FileSizeBytes / 1024.0:F0} KB";

        public ReactiveCommand<Unit, Unit> ShowInFinderCommand { get; }
        public ReactiveCommand<Unit, Unit> DeleteCommand { get; }

        public LocalAssetViewModel(string assetId, string resolution, string filePath,
            Action<LocalAssetViewModel> onShowInFinder,
            Func<LocalAssetViewModel, System.Threading.Tasks.Task> onDelete)
        {
            AssetId = assetId;
            Resolution = resolution;
            FilePath = filePath;
            FileName = Path.GetFileName(filePath);
            Format = Path.GetExtension(filePath).TrimStart('.').ToLowerInvariant();
            FileSizeBytes = File.Exists(filePath) ? new FileInfo(filePath).Length : 0;
            ShowInFinderCommand = ReactiveCommand.Create(() => onShowInFinder(this));
            DeleteCommand = ReactiveCommand.CreateFromTask(() => onDelete(this));
        }
    }
}
