using ReactiveUI;

namespace Panzerfaust.ViewModels
{
    internal enum DownloadOperationStatus { Pending, Downloading, Extracting, Done, Failed }

    internal class DownloadOperationViewModel : ReactiveObject
    {
        private DownloadOperationStatus _status = DownloadOperationStatus.Pending;
        public DownloadOperationStatus Status
        {
            get => _status;
            set
            {
                this.RaiseAndSetIfChanged(ref _status, value);
                this.RaisePropertyChanged(nameof(StatusLabel));
                this.RaisePropertyChanged(nameof(IsDone));
                this.RaisePropertyChanged(nameof(IsFailed));
                this.RaisePropertyChanged(nameof(IsActive));
                this.RaisePropertyChanged(nameof(BadgeColor));
            }
        }

        private int _progress;
        public int Progress
        {
            get => _progress;
            set
            {
                this.RaiseAndSetIfChanged(ref _progress, value);
                this.RaisePropertyChanged(nameof(ProgressLabel));
            }
        }

        private string _errorMessage = string.Empty;
        public string ErrorMessage
        {
            get => _errorMessage;
            set => this.RaiseAndSetIfChanged(ref _errorMessage, value);
        }

        public string Version { get; }

        public bool IsDone => _status == DownloadOperationStatus.Done;
        public bool IsFailed => _status == DownloadOperationStatus.Failed;
        public bool IsActive => _status is DownloadOperationStatus.Downloading or DownloadOperationStatus.Extracting;

        public string StatusLabel => _status switch
        {
            DownloadOperationStatus.Pending => "Pending",
            DownloadOperationStatus.Downloading => "Downloading",
            DownloadOperationStatus.Extracting => "Extracting…",
            DownloadOperationStatus.Done => "Completed",
            DownloadOperationStatus.Failed => "Failed",
            _ => string.Empty
        };

        public string ProgressLabel => _status == DownloadOperationStatus.Downloading ? $"{_progress}%" : string.Empty;

        public string BadgeColor => _status switch
        {
            DownloadOperationStatus.Done => "#1E7E34",
            DownloadOperationStatus.Failed => "#C42B1C",
            DownloadOperationStatus.Downloading or DownloadOperationStatus.Extracting => "#0078D4",
            _ => "#666666"
        };

        public DownloadOperationViewModel(string version)
        {
            Version = version;
        }
    }
}
