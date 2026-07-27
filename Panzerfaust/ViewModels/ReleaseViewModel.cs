using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.Linq;
using System.Reactive;
using System.Runtime.InteropServices;

namespace Panzerfaust.ViewModels
{
    internal class ReleaseViewModel : ReactiveObject
    {
        private readonly GitHubRelease _release;

        public string Tag => _release.TagName;
        public string DisplayName => string.IsNullOrEmpty(_release.Name) ? _release.TagName : _release.Name;
        public bool IsPrerelease => _release.Prerelease;
        public string Badge => _release.Prerelease ? "RC" : "Stable";
        public string BadgeColor => _release.Prerelease ? "#C07C00" : "#1E7E34";
        public string PublishedDate
        {
            get
            {
                if (DateTime.TryParse(_release.PublishedAt, out var dt))
                    return dt.ToString("MMM d, yyyy");
                return _release.PublishedAt;
            }
        }

        public string? PlatformDownloadUrl
        {
            get
            {
                Func<string, bool> match;
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
                    match = n => n.Contains("windows", StringComparison.OrdinalIgnoreCase)
                              || n.Contains("win64",   StringComparison.OrdinalIgnoreCase)
                              || n.Contains("win32",   StringComparison.OrdinalIgnoreCase);
                else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
                    match = n => n.Contains("Darwin",  StringComparison.OrdinalIgnoreCase)
                              || n.Contains("macos",   StringComparison.OrdinalIgnoreCase)
                              || n.Contains("osx",     StringComparison.OrdinalIgnoreCase);
                else
                    match = n => n.Contains("linux",   StringComparison.OrdinalIgnoreCase)
                              || n.Contains("ubuntu",  StringComparison.OrdinalIgnoreCase);

                return _release.Assets.FirstOrDefault(a => match(a.Name))?.DownloadUrl;
            }
        }

        public bool HasDownload => PlatformDownloadUrl != null;

        private bool _isDownloading;
        public bool IsDownloading
        {
            get => _isDownloading;
            set
            {
                this.RaiseAndSetIfChanged(ref _isDownloading, value);
                this.RaisePropertyChanged(nameof(DownloadLabel));
            }
        }

        private int _downloadProgress;
        public int DownloadProgress
        {
            get => _downloadProgress;
            set => this.RaiseAndSetIfChanged(ref _downloadProgress, value);
        }

        public string DownloadLabel => _isDownloading ? "Downloading…" : "↓ Download";

        public ReactiveCommand<Unit, Unit> DownloadCommand { get; }

        public ReleaseViewModel(GitHubRelease release, Func<ReleaseViewModel, System.Threading.Tasks.Task> downloadHandler)
        {
            _release = release;
            DownloadCommand = ReactiveCommand.CreateFromTask(
                () => downloadHandler(this),
                this.WhenAnyValue(x => x.IsDownloading, downloading => !downloading));
        }
    }
}
