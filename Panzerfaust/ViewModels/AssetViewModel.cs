using Avalonia.Media.Imaging;
using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.IO;
using System.Net.Http;
using System.Reactive;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class AssetViewModel : ReactiveObject
    {
        private static readonly HttpClient _thumbClient = new();

        private readonly PolyHavenAsset _asset;

        public string Id => _asset.Id;
        public string Name => _asset.Name;
        public int PolyCount => _asset.PolyCount;
        public int DownloadCount => _asset.DownloadCount;
        public string PolyCountLabel => _asset.PolyCount > 0 ? $"{_asset.PolyCount:N0} polys" : string.Empty;
        public string Categories => string.Join(", ", _asset.Categories);

        private Bitmap? _thumbnail;
        public Bitmap? Thumbnail
        {
            get => _thumbnail;
            private set => this.RaiseAndSetIfChanged(ref _thumbnail, value);
        }

        private bool _isLoadingThumb;
        public bool IsLoadingThumb
        {
            get => _isLoadingThumb;
            private set => this.RaiseAndSetIfChanged(ref _isLoadingThumb, value);
        }

        public ReactiveCommand<Unit, Unit> ShowDetailCommand { get; }

        public AssetViewModel(PolyHavenAsset asset, Action<AssetViewModel> onShowDetail)
        {
            _asset = asset;
            ShowDetailCommand = ReactiveCommand.Create(() => onShowDetail(this));
        }

        public async Task LoadThumbnailAsync()
        {
            if (_thumbnail != null) return;
            IsLoadingThumb = true;
            try
            {
                var bytes = await _thumbClient.GetByteArrayAsync(_asset.ThumbnailUrl);
                using var ms = new MemoryStream(bytes);
                Thumbnail = new Bitmap(ms);
            }
            catch { }
            finally { IsLoadingThumb = false; }
        }
    }
}
