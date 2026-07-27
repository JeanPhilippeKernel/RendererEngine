using ReactiveUI;
using System;
using System.Collections.ObjectModel;
using System.Reactive;
using System.Reactive.Linq;

namespace Panzerfaust.ViewModels
{
    internal class AssetFormatOption : ReactiveObject
    {
        public string Format { get; }
        public string Resolution { get; }
        public string Url { get; }
        public string Label => $"{Format.ToUpperInvariant()} · {Resolution}";

        private bool _isSelected;
        public bool IsSelected
        {
            get => _isSelected;
            set => this.RaiseAndSetIfChanged(ref _isSelected, value);
        }

        public AssetFormatOption(string format, string resolution, string url)
        {
            Format = format;
            Resolution = resolution;
            Url = url;
        }
    }

    internal class AssetDetailViewModel : ReactiveObject
    {
        public AssetViewModel Asset { get; }

        public ObservableCollection<AssetFormatOption> FormatOptions { get; } = new();

        private AssetFormatOption? _selectedFormat;
        public AssetFormatOption? SelectedFormat
        {
            get => _selectedFormat;
            set => this.RaiseAndSetIfChanged(ref _selectedFormat, value);
        }

        private bool _isLoadingFormats;
        public bool IsLoadingFormats
        {
            get => _isLoadingFormats;
            set => this.RaiseAndSetIfChanged(ref _isLoadingFormats, value);
        }

        public ReactiveCommand<Unit, Unit> DownloadCommand { get; }
        public ReactiveCommand<Unit, Unit> CloseCommand { get; }

        public AssetDetailViewModel(AssetViewModel asset,
            Func<AssetDetailViewModel, System.Threading.Tasks.Task> downloadHandler,
            Action closeHandler)
        {
            Asset = asset;
            DownloadCommand = ReactiveCommand.CreateFromTask(
                () => downloadHandler(this),
                this.WhenAnyValue(x => x.SelectedFormat).Select(f => f != null));
            CloseCommand = ReactiveCommand.Create(closeHandler);
        }
    }
}
