using Avalonia.Styling;
using DynamicData;
using Panzerfaust.Models;
using Panzerfaust.Service;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Reactive;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class MainWindowViewModel : ViewModelBase
    {
        private static readonly string SettingsPath = Models.AppPaths.SettingsFile;

        private readonly IProjectService _projectService;
        private readonly IEngineService _engineService;
        private readonly IStorageProviderService _storageService;
        private readonly IGitHubReleaseService _releaseService;
        private readonly IPolyHavenService _polyHavenService;
        private readonly Func<ProjectWindowViewModel> _projectWindowVmFactory;
        private readonly HttpClient _downloadClient = new();

        private bool _isSidebarExpanded = true;
        public bool IsSidebarExpanded
        {
            get => _isSidebarExpanded;
            set => this.RaiseAndSetIfChanged(ref _isSidebarExpanded, value);
        }

        private string _selectedPage = "Projects";
        public string SelectedPage
        {
            get => _selectedPage;
            set => this.RaiseAndSetIfChanged(ref _selectedPage, value);
        }

        private string _selectedTheme = "Dark";
        public string SelectedTheme
        {
            get => _selectedTheme;
            set
            {
                this.RaiseAndSetIfChanged(ref _selectedTheme, value);
                this.RaisePropertyChanged(nameof(IsDarkTheme));
                this.RaisePropertyChanged(nameof(IsLightTheme));
                if (Avalonia.Application.Current != null)
                    Avalonia.Application.Current.RequestedThemeVariant =
                        value == "Light" ? ThemeVariant.Light : ThemeVariant.Dark;
                SaveSettings();
            }
        }

        public bool IsDarkTheme => _selectedTheme == "Dark";
        public bool IsLightTheme => _selectedTheme == "Light";

        private string _searchText = string.Empty;
        public string SearchText
        {
            get => _searchText;
            set => this.RaiseAndSetIfChanged(ref _searchText, value);
        }

        private readonly SourceList<ProjectViewModel> _projects = new();
        public ReadOnlyObservableCollection<ProjectViewModel> FilteredProjects { get; }

        private int _projectCount;
        public int ProjectCount
        {
            get => _projectCount;
            private set => this.RaiseAndSetIfChanged(ref _projectCount, value);
        }

        private string _statusBarMessage = string.Empty;
        public string StatusBarMessage
        {
            get => _statusBarMessage;
            private set => this.RaiseAndSetIfChanged(ref _statusBarMessage, value);
        }

        public ObservableCollection<BackgroundTaskViewModel> BackgroundTasks { get; } = new();
        public ObservableCollection<InstalledEngineViewModel> InstalledEngines { get; } = new();
        public ObservableCollection<DownloadOperationViewModel> EngineDownloads { get; } = new();
        public bool HasEngineDownloads => EngineDownloads.Count > 0;

        public ObservableCollection<DownloadOperationViewModel> AssetDownloads { get; } = new();
        public bool HasAssetDownloads => AssetDownloads.Count > 0;

        private void TrackDownload(DownloadOperationViewModel op, string category)
        {
            op.WhenAnyValue(x => x.Status).Subscribe(_ => SaveDownloadHistory());
        }

        private void SaveDownloadHistory()
        {
            try
            {
                var entries = new List<Models.DownloadHistoryEntry>();
                foreach (var op in EngineDownloads)
                    entries.Add(new Models.DownloadHistoryEntry { Category = "engine", Version = op.Version, Status = op.Status.ToString(), Progress = op.Progress, ErrorMessage = op.ErrorMessage });
                foreach (var op in AssetDownloads)
                    entries.Add(new Models.DownloadHistoryEntry { Category = "asset", Version = op.Version, Status = op.Status.ToString(), Progress = op.Progress, ErrorMessage = op.ErrorMessage });
                File.WriteAllText(Models.AppPaths.DownloadHistoryFile, JsonSerializer.Serialize(entries, new JsonSerializerOptions { WriteIndented = true }));
            }
            catch { }
        }

        private void LoadDownloadHistory()
        {
            try
            {
                if (!File.Exists(Models.AppPaths.DownloadHistoryFile)) return;
                var entries = JsonSerializer.Deserialize<List<Models.DownloadHistoryEntry>>(File.ReadAllText(Models.AppPaths.DownloadHistoryFile));
                if (entries == null) return;
                foreach (var e in entries)
                {
                    var status = Enum.TryParse<DownloadOperationStatus>(e.Status, out var s) ? s : DownloadOperationStatus.Failed;
                    // Skip in-progress entries from a previous session — they never completed
                    if (status is DownloadOperationStatus.Downloading or DownloadOperationStatus.Extracting or DownloadOperationStatus.Pending)
                        status = DownloadOperationStatus.Failed;
                    var op = new DownloadOperationViewModel(e.Version) { Status = status, Progress = e.Progress, ErrorMessage = status == DownloadOperationStatus.Failed && string.IsNullOrEmpty(e.ErrorMessage) ? "Interrupted" : e.ErrorMessage };
                    if (e.Category == "engine") { EngineDownloads.Add(op); TrackDownload(op, "engine"); }
                    else { AssetDownloads.Add(op); TrackDownload(op, "asset"); }
                }
                this.RaisePropertyChanged(nameof(HasEngineDownloads));
                this.RaisePropertyChanged(nameof(HasAssetDownloads));
            }
            catch { }
        }

        private readonly ObservableCollection<AssetViewModel> _allAssets = new();
        public ObservableCollection<AssetViewModel> FilteredAssets { get; } = new();
        public ObservableCollection<LocalAssetViewModel> LocalAssets { get; } = new();
        public bool HasLocalAssets => LocalAssets.Count > 0;

        private string _assetSearchText = string.Empty;
        public string AssetSearchText
        {
            get => _assetSearchText;
            set
            {
                this.RaiseAndSetIfChanged(ref _assetSearchText, value);
                ApplyAssetFilter();
            }
        }

        private bool _isLoadingAssets;
        public bool IsLoadingAssets
        {
            get => _isLoadingAssets;
            set => this.RaiseAndSetIfChanged(ref _isLoadingAssets, value);
        }

        private string _assetsError = string.Empty;
        public string AssetsError
        {
            get => _assetsError;
            set => this.RaiseAndSetIfChanged(ref _assetsError, value);
        }

        private AssetDetailViewModel? _selectedAssetDetail;
        public AssetDetailViewModel? SelectedAssetDetail
        {
            get => _selectedAssetDetail;
            set
            {
                this.RaiseAndSetIfChanged(ref _selectedAssetDetail, value);
                this.RaisePropertyChanged(nameof(IsAssetDetailOpen));
            }
        }

        public bool IsAssetDetailOpen => _selectedAssetDetail != null;

        private bool _isTaskPanelOpen;
        public bool IsTaskPanelOpen
        {
            get => _isTaskPanelOpen;
            set => this.RaiseAndSetIfChanged(ref _isTaskPanelOpen, value);
        }

        private int _failedTaskCount;
        public int FailedTaskCount
        {
            get => _failedTaskCount;
            private set
            {
                this.RaiseAndSetIfChanged(ref _failedTaskCount, value);
                this.RaisePropertyChanged(nameof(HasFailedTasks));
            }
        }

        public bool HasFailedTasks => _failedTaskCount > 0;

        private ProjectViewModel? _selectedProject;
        public ProjectViewModel? SelectedProject
        {
            get => _selectedProject;
            set => this.RaiseAndSetIfChanged(ref _selectedProject, value);
        }

        private bool _isInfoPanelOpen;
        public bool IsInfoPanelOpen
        {
            get => _isInfoPanelOpen;
            set => this.RaiseAndSetIfChanged(ref _isInfoPanelOpen, value);
        }

        private string _defaultProjectLocation = Models.AppPaths.Projects;
        public string DefaultProjectLocation
        {
            get => _defaultProjectLocation;
            set
            {
                this.RaiseAndSetIfChanged(ref _defaultProjectLocation, value);
                SaveSettings();
            }
        }

        private string _engineInstallLocation = Models.AppPaths.Engines;
        public string EngineInstallLocation
        {
            get => _engineInstallLocation;
            set
            {
                this.RaiseAndSetIfChanged(ref _engineInstallLocation, value);
                SaveSettings();
                RefreshInstalledEngines();
            }
        }

        private ObservableCollection<ReleaseViewModel> _releases = new();
        public ObservableCollection<ReleaseViewModel> Releases => _releases;

        private ReleaseViewModel? _selectedRelease;
        public ReleaseViewModel? SelectedRelease
        {
            get => _selectedRelease;
            set => this.RaiseAndSetIfChanged(ref _selectedRelease, value);
        }

        private bool _isLoadingReleases;
        public bool IsLoadingReleases
        {
            get => _isLoadingReleases;
            set => this.RaiseAndSetIfChanged(ref _isLoadingReleases, value);
        }

        private string _releasesError = string.Empty;
        public string ReleasesError
        {
            get => _releasesError;
            set => this.RaiseAndSetIfChanged(ref _releasesError, value);
        }

        private CancellationTokenSource? _toastCts;

        private bool _isToastVisible;
        public bool IsToastVisible
        {
            get => _isToastVisible;
            set => this.RaiseAndSetIfChanged(ref _isToastVisible, value);
        }

        private string _toastText = string.Empty;
        public string ToastText
        {
            get => _toastText;
            set => this.RaiseAndSetIfChanged(ref _toastText, value);
        }

        private bool _isToastExpanded;
        public bool IsToastExpanded
        {
            get => _isToastExpanded;
            set => this.RaiseAndSetIfChanged(ref _isToastExpanded, value);
        }

        private TaskCompletionSource<bool>? _deleteTcs;

        private bool _isDeleteModalOpen;
        public bool IsDeleteModalOpen
        {
            get => _isDeleteModalOpen;
            set => this.RaiseAndSetIfChanged(ref _isDeleteModalOpen, value);
        }

        private string _deleteModalProjectName = string.Empty;
        public string DeleteModalProjectName
        {
            get => _deleteModalProjectName;
            set => this.RaiseAndSetIfChanged(ref _deleteModalProjectName, value);
        }

        private string _deleteConfirmText = string.Empty;
        public string DeleteConfirmText
        {
            get => _deleteConfirmText;
            set => this.RaiseAndSetIfChanged(ref _deleteConfirmText, value);
        }

        public ReactiveCommand<Unit, Unit> CreateProjectCommand { get; }
        public ReactiveCommand<Unit, Unit> ToggleSidebarCommand { get; }
        public ReactiveCommand<string, Unit> NavigateCommand { get; }
        public ReactiveCommand<string, Unit> SelectThemeCommand { get; }
        public ReactiveCommand<Unit, Unit> CloseInfoCommand { get; }
        public ReactiveCommand<Unit, Unit> ConfirmDeleteCommand { get; }
        public ReactiveCommand<Unit, Unit> CancelDeleteCommand { get; }
        public ReactiveCommand<Unit, Unit> BrowseDefaultLocationCommand { get; }
        public ReactiveCommand<Unit, Unit> BrowseEngineInstallLocationCommand { get; }
        public ReactiveCommand<Unit, Unit> ToggleToastCommand { get; }
        public ReactiveCommand<Unit, Unit> ToggleTaskPanelCommand { get; }
        public Interaction<ProjectWindowViewModel, ProjectViewModel?> NewProjectDialog { get; } = new();
        public Interaction<string, bool> DeleteProjectInteraction { get; } = new();
        public Interaction<string, bool> ConfirmOverwriteInteraction { get; } = new();
        public Interaction<string, bool> ConfirmDeleteAssetInteraction { get; } = new();
        public Interaction<EnginePickerViewModel, InstalledEngineViewModel?> EnginePickerInteraction { get; } = new();
        public Interaction<string, bool> ConfirmUninstallEngineInteraction { get; } = new();

        public MainWindowViewModel(IProjectService projectService, IEngineService engineService, IStorageProviderService storageService, IGitHubReleaseService releaseService, IPolyHavenService polyHavenService, Func<ProjectWindowViewModel> projectWindowVmFactory)
        {
            _projectService = projectService;
            _engineService = engineService;
            _storageService = storageService;
            _releaseService = releaseService;
            _polyHavenService = polyHavenService;
            _projectWindowVmFactory = projectWindowVmFactory;
            LoadSettings();
            RefreshInstalledEngines();
            ScanLocalAssets();
            LoadDownloadHistory();

            var filterPredicate = this.WhenAnyValue(x => x.SearchText)
                .Select(text => CreateFilterPredicate(text.Trim()))
                .DistinctUntilChanged();

            _projects.Connect()
                .Filter(filterPredicate)
                .Bind(out var filteredProjects)
                .Subscribe();

            FilteredProjects = filteredProjects;

            _projects.Connect()
                .Filter(filterPredicate)
                .ToCollection()
                .Subscribe(col => ProjectCount = col.Count);

            RxApp.MainThreadScheduler.Schedule(() => { _ = LoadProjectsAsync(); });

            var canConfirm = this.WhenAnyValue(
                x => x.DeleteConfirmText,
                x => x.DeleteModalProjectName,
                (text, name) => text == name && !string.IsNullOrEmpty(name));

            CreateProjectCommand = ReactiveCommand.CreateFromTask(OnCreateProjectCommand);
            ToggleSidebarCommand = ReactiveCommand.Create(() => { IsSidebarExpanded = !IsSidebarExpanded; });
            NavigateCommand = ReactiveCommand.Create<string>(page => { SelectedPage = page; });
            SelectThemeCommand = ReactiveCommand.Create<string>(theme => { SelectedTheme = theme; });
            CloseInfoCommand = ReactiveCommand.Create(() => { IsInfoPanelOpen = false; SelectedProject = null; });
            ConfirmDeleteCommand = ReactiveCommand.Create(OnConfirmDelete, canConfirm);
            CancelDeleteCommand = ReactiveCommand.Create(OnCancelDelete);
            BrowseDefaultLocationCommand = ReactiveCommand.CreateFromTask(OnBrowseDefaultLocation);
            BrowseEngineInstallLocationCommand = ReactiveCommand.CreateFromTask(OnBrowseEngineInstallLocation);
            ToggleToastCommand = ReactiveCommand.Create(() => { IsToastExpanded = !IsToastExpanded; });
            ToggleTaskPanelCommand = ReactiveCommand.Create(() => { IsTaskPanelOpen = !IsTaskPanelOpen; });

            DeleteProjectInteraction.RegisterHandler(async ctx =>
            {
                DeleteModalProjectName = ctx.Input;
                DeleteConfirmText = string.Empty;
                IsDeleteModalOpen = true;
                _deleteTcs = new TaskCompletionSource<bool>();
                var result = await _deleteTcs.Task;
                ctx.SetOutput(result);
            });

            MessageBus.Current.Listen<(string, ProjectViewModel)>().Subscribe(OnReceiveMessage);
            MessageBus.Current.Listen<(string, string)>().Subscribe(OnReceiveStringMessage);

            RxApp.MainThreadScheduler.Schedule(() => { _ = RunBackgroundTask("Fetch engine releases", FetchReleasesCore); });
            RxApp.MainThreadScheduler.Schedule(() => { _ = RunBackgroundTask("Fetch asset store", FetchAssetsCore); });
        }

        private static int[] BuildKMPTable(string pattern)
        {
            int j = 0;
            int m = pattern.Length;
            int[] lps = new int[m];

            lps[0] = 0;
            for (int i = 1; i < m; i++)
            {
                while (j > 0 && char.ToLower(pattern[i]) != char.ToLower(pattern[j]))
                    j = lps[j - 1];
                if (char.ToLower(pattern[i]) == char.ToLower(pattern[j]))
                    j++;
                lps[i] = j;
            }
            return lps;
        }

        private static bool KMPSearch(string text, string pattern)
        {
            if (string.IsNullOrEmpty(pattern)) return true;
            if (string.IsNullOrEmpty(text)) return false;
            if (pattern.Length > text.Length) return false;

            int j = 0;
            int[] lps = BuildKMPTable(pattern);

            for (int i = 0; i < text.Length; i++)
            {
                while (j > 0 && char.ToLower(text[i]) != char.ToLower(pattern[j]))
                    j = lps[j - 1];
                if (char.ToLower(text[i]) == char.ToLower(pattern[j]))
                    j++;
                if (j == pattern.Length)
                    return true;
            }
            return false;
        }

        private static Func<ProjectViewModel, bool> CreateFilterPredicate(string searchTerm)
        {
            return string.IsNullOrWhiteSpace(searchTerm)
                ? _ => true
                : p => KMPSearch(p.Name, searchTerm);
        }

        private void OnReceiveMessage((string, ProjectViewModel) message)
        {
            var (action, data) = message;
            if (action == Message.DeleteAction)
            {
                if (SelectedProject == data) { IsInfoPanelOpen = false; SelectedProject = null; }
                _projects.Remove(data);
            }
            else if (action == Message.ShowInfoAction)
            {
                SelectedProject = data;
                IsInfoPanelOpen = true;
            }
        }

        private async Task OnCreateProjectCommand()
        {
            var projectViewModel = _projectWindowVmFactory();
            var result = await NewProjectDialog.Handle(projectViewModel);
            if (result != null)
            {
                result.SetRemovalInteraction(DeleteProjectInteraction);
                result.SetEnginePickerInteraction(EnginePickerInteraction, InstalledEngines);
                _projects.Add(result);
            }
        }

        private async Task RunBackgroundTask(string label, Func<Task> work)
        {
            var existing = BackgroundTasks.FirstOrDefault(t => t.Label == label);
            BackgroundTaskViewModel task;
            if (existing != null)
            {
                task = existing;
            }
            else
            {
                task = new BackgroundTaskViewModel(label, work);
                task.RetryCommand.Subscribe(_ => UpdateFailedCount());
                BackgroundTasks.Add(task);
            }

            StatusBarMessage = $"{label}…";
            await task.RunAsync();
            UpdateFailedCount();

            if (task.IsSucceeded)
                StatusBarMessage = $"{label} — done";
            else
            {
                StatusBarMessage = $"{label} — failed";
                _ = ShowToastAsync($"{label}: {task.ErrorMessage}");
            }
        }

        private void UpdateFailedCount() =>
            FailedTaskCount = BackgroundTasks.Count(t => t.IsFailed);

        private async Task FetchReleasesCore()
        {
            IsLoadingReleases = true;
            ReleasesError = string.Empty;
            try
            {
                var releases = await _releaseService.GetReleasesAsync();
                _releases.Clear();
                foreach (var r in releases)
                    _releases.Add(new ReleaseViewModel(r, DownloadReleaseAsync));
                if (_releases.Count > 0)
                    SelectedRelease = _releases[0];
            }
            catch
            {
                throw;
            }
            finally
            {
                IsLoadingReleases = false;
            }
        }

        private async Task FetchReleasesAsync() =>
            await RunBackgroundTask("Fetch engine releases", FetchReleasesCore);

        private void OnReceiveStringMessage((string, string) message)
        {
            var (action, text) = message;
            if (action == Message.ToastErrorAction)
                _ = ShowToastAsync(text);
        }

        private async Task ShowToastAsync(string text)
        {
            _toastCts?.Cancel();
            _toastCts = new CancellationTokenSource();
            var token = _toastCts.Token;

            ToastText = text;
            IsToastExpanded = false;
            IsToastVisible = true;

            try { await Task.Delay(4000, token); }
            catch (TaskCanceledException) { return; }

            IsToastVisible = false;
        }

        private async Task OnBrowseDefaultLocation()
        {
            var folder = await _storageService.PickDirectoryAsync();
            if (folder != null)
                DefaultProjectLocation = folder.Path.LocalPath;
        }

        private async Task OnBrowseEngineInstallLocation()
        {
            var folder = await _storageService.PickDirectoryAsync();
            if (folder != null)
                EngineInstallLocation = folder.Path.LocalPath;
        }

        private async Task DownloadReleaseAsync(ReleaseViewModel release)
        {
            var url = release.PlatformDownloadUrl;
            if (string.IsNullOrEmpty(url)) return;

            var destDir = Path.Combine(_engineInstallLocation, release.Tag);
            if (Directory.Exists(destDir))
            {
                _ = ShowToastAsync($"{release.Tag} is already installed.");
                return;
            }

            var op = EngineDownloads.FirstOrDefault(o => o.Version == release.Tag)
                     ?? new DownloadOperationViewModel(release.Tag);
            if (!EngineDownloads.Contains(op))
                Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                {
                    EngineDownloads.Insert(0, op);
                    TrackDownload(op, "engine");
                    this.RaisePropertyChanged(nameof(HasEngineDownloads));
                });

            await RunBackgroundTask($"Download {release.Tag}", () => Task.Run(async () =>
            {
                async Task UI(System.Action a) =>
                    await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(a);

                await UI(() => { op.Status = DownloadOperationStatus.Downloading; op.Progress = 0; op.ErrorMessage = string.Empty; release.IsDownloading = true; release.DownloadProgress = 0; });
                try
                {
                    Directory.CreateDirectory(destDir);

                    using var response = await _downloadClient.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
                    response.EnsureSuccessStatusCode();
                    var total = response.Content.Headers.ContentLength ?? -1L;

                    var archivePath = Path.Combine(destDir, Path.GetFileName(url));
                    using (var fs = File.Create(archivePath))
                    using (var stream = await response.Content.ReadAsStreamAsync())
                    {
                        var buffer = new byte[81920];
                        long downloaded = 0;
                        int read;
                        int lastReported = -1;
                        while ((read = await stream.ReadAsync(buffer)) > 0)
                        {
                            await fs.WriteAsync(buffer.AsMemory(0, read));
                            downloaded += read;
                            if (total > 0)
                            {
                                var pct = (int)(downloaded * 100 / total);
                                if (pct != lastReported)
                                {
                                    lastReported = pct;
                                    await UI(() => { op.Progress = pct; release.DownloadProgress = pct; });
                                }
                            }
                        }
                    }

                    await UI(() => { op.Status = DownloadOperationStatus.Extracting; op.Progress = 0; });

                    if (archivePath.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                    {
                        using var zip = ZipFile.OpenRead(archivePath);
                        int total_entries = zip.Entries.Count;
                        int done = 0;
                        foreach (var entry in zip.Entries)
                        {
                            if (string.IsNullOrEmpty(entry.Name))
                            {
                                Directory.CreateDirectory(Path.Combine(destDir, entry.FullName));
                            }
                            else
                            {
                                var entryDest = Path.Combine(destDir, entry.FullName);
                                Directory.CreateDirectory(Path.GetDirectoryName(entryDest)!);
                                entry.ExtractToFile(entryDest, overwrite: true);
                            }
                            done++;
                            var pct = (int)(done * 100.0 / total_entries);
                            await UI(() => op.Progress = pct);
                        }
                    }
                    else if (archivePath.EndsWith(".tar.gz", StringComparison.OrdinalIgnoreCase)
                          || archivePath.EndsWith(".tgz", StringComparison.OrdinalIgnoreCase))
                    {
                        // Use system tar — available on macOS, Linux, and Windows 10+
                        var tarInfo = new System.Diagnostics.ProcessStartInfo("tar", $"-xzf \"{archivePath}\" -C \"{destDir}\"")
                        {
                            UseShellExecute = false,
                            CreateNoWindow = true
                        };
                        Directory.CreateDirectory(destDir);
                        var tarProc = System.Diagnostics.Process.Start(tarInfo)!;
                        await tarProc.WaitForExitAsync();
                        if (tarProc.ExitCode != 0)
                            throw new Exception($"tar extraction failed with exit code {tarProc.ExitCode}");
                        await UI(() => op.Progress = 100);
                    }

                    File.Delete(archivePath);

                    await UI(() =>
                    {
                        op.Status = DownloadOperationStatus.Done;
                        op.Progress = 100;
                        release.IsDownloading = false;
                        release.DownloadProgress = 100;
                        RefreshInstalledEngines();
                    });
                }
                catch (Exception ex)
                {
                    await UI(() =>
                    {
                        op.Status = DownloadOperationStatus.Failed;
                        op.ErrorMessage = ex.Message;
                        release.IsDownloading = false;
                        release.DownloadProgress = 0;
                    });
                    throw;
                }
            }));
        }

        private async Task FetchAssetsCore()
        {
            IsLoadingAssets = true;
            AssetsError = string.Empty;
            try
            {
                var assets = (await _polyHavenService.GetModelsAsync()).ToList();
                await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(() =>
                {
                    _allAssets.Clear();
                    foreach (var a in assets)
                        _allAssets.Add(new AssetViewModel(a, OnShowAssetDetail));
                    ApplyAssetFilter();
                });
                _ = Task.Run(async () =>
                {
                    foreach (var vm in _allAssets)
                        await vm.LoadThumbnailAsync();
                });
            }
            catch
            {
                throw;
            }
            finally
            {
                IsLoadingAssets = false;
            }
        }

        private void ApplyAssetFilter()
        {
            var term = _assetSearchText.Trim();
            FilteredAssets.Clear();
            foreach (var a in _allAssets)
            {
                if (string.IsNullOrEmpty(term) || KMPSearch(a.Name, term))
                    FilteredAssets.Add(a);
            }
        }

        private void OnShowAssetDetail(AssetViewModel asset)
        {
            var detail = new AssetDetailViewModel(asset, LoadAndDownloadAssetAsync, () => SelectedAssetDetail = null);
            SelectedAssetDetail = detail;
            _ = LoadAssetFormatsAsync(detail);
        }

        private async Task LoadAssetFormatsAsync(AssetDetailViewModel detail)
        {
            detail.IsLoadingFormats = true;
            try
            {
                var files = await _polyHavenService.GetFilesAsync(detail.Asset.Id);
                await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(() =>
                {
                    detail.FormatOptions.Clear();
                    var preferredFormats = new[] { "gltf", "fbx", "usd", "blend" };
                    var resolutions = new[] { "4k", "2k", "1k" };
                    foreach (var fmt in preferredFormats)
                    {
                        if (files.TryGetPropertyValue(fmt, out var fmtNode) && fmtNode is System.Text.Json.Nodes.JsonObject fmtObj)
                        {
                            foreach (var res in resolutions)
                            {
                                if (fmtObj.TryGetPropertyValue(res, out var resNode) && resNode is System.Text.Json.Nodes.JsonObject resObj)
                                {
                                    if (resObj.TryGetPropertyValue(fmt, out var fileNode) && fileNode is System.Text.Json.Nodes.JsonObject fileObj)
                                    {
                                        var url = fileObj["url"]?.GetValue<string>() ?? string.Empty;
                                        if (!string.IsNullOrEmpty(url))
                                            detail.FormatOptions.Add(new AssetFormatOption(fmt, res, url));
                                    }
                                }
                            }
                        }
                    }
                    if (detail.FormatOptions.Count > 0)
                    {
                        detail.SelectedFormat = detail.FormatOptions[0];
                        detail.SelectedFormat.IsSelected = true;
                    }
                });
            }
            catch (Exception ex)
            {
                _ = ShowToastAsync($"Failed to load formats for {detail.Asset.Name}: {ex.Message}");
            }
            finally
            {
                detail.IsLoadingFormats = false;
            }
        }

        private async Task LoadAndDownloadAssetAsync(AssetDetailViewModel detail)
        {
            if (detail.SelectedFormat == null) return;
            var fmt = detail.SelectedFormat;
            var label = $"{detail.Asset.Name} · {fmt.Resolution} {fmt.Format.ToUpperInvariant()}";
            var fileName = System.IO.Path.GetFileName(fmt.Url);
            var destPath = System.IO.Path.Combine(Models.AppPaths.Assets, detail.Asset.Id, fmt.Resolution, fileName);

            if (File.Exists(destPath))
            {
                var overwrite = await ConfirmOverwriteInteraction.Handle(
                    $"{detail.Asset.Name} ({fmt.Resolution} {fmt.Format.ToUpperInvariant()}) is already downloaded. Overwrite?");
                if (!overwrite) return;
            }

            var op = AssetDownloads.FirstOrDefault(o => o.Version == label)
                     ?? new DownloadOperationViewModel(label);
            if (!AssetDownloads.Contains(op))
                await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(() =>
                {
                    AssetDownloads.Insert(0, op);
                    TrackDownload(op, "asset");
                    this.RaisePropertyChanged(nameof(HasAssetDownloads));
                });

            await RunBackgroundTask($"Download {label}", () => Task.Run(async () =>
            {
                async Task UI(System.Action a) =>
                    await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(a);

                await UI(() => { op.Status = DownloadOperationStatus.Downloading; op.Progress = 0; op.ErrorMessage = string.Empty; });
                try
                {
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(destPath)!);
                    using var response = await _downloadClient.GetAsync(fmt.Url, HttpCompletionOption.ResponseHeadersRead);
                    response.EnsureSuccessStatusCode();
                    var total = response.Content.Headers.ContentLength ?? -1L;
                    using var fs = File.Create(destPath);
                    using var stream = await response.Content.ReadAsStreamAsync();
                    var buffer = new byte[81920];
                    long downloaded = 0;
                    int lastPct = -1, read;
                    while ((read = await stream.ReadAsync(buffer)) > 0)
                    {
                        await fs.WriteAsync(buffer.AsMemory(0, read));
                        downloaded += read;
                        if (total > 0)
                        {
                            var pct = (int)(downloaded * 100 / total);
                            if (pct != lastPct) { lastPct = pct; await UI(() => { op.Progress = pct; }); }
                        }
                    }
                    await UI(() => { op.Status = DownloadOperationStatus.Done; op.Progress = 100; });
                    await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(ScanLocalAssets);
                }
                catch (Exception ex)
                {
                    await UI(() => { op.Status = DownloadOperationStatus.Failed; op.ErrorMessage = ex.Message; });
                    throw;
                }
            }));
        }

        private void ScanLocalAssets()
        {
            LocalAssets.Clear();
            var root = Models.AppPaths.Assets;
            if (!Directory.Exists(root)) return;
            foreach (var assetDir in Directory.EnumerateDirectories(root))
            {
                var assetId = Path.GetFileName(assetDir);
                foreach (var resDir in Directory.EnumerateDirectories(assetDir))
                {
                    var resolution = Path.GetFileName(resDir);
                    foreach (var file in Directory.EnumerateFiles(resDir))
                        LocalAssets.Add(new LocalAssetViewModel(assetId, resolution, file, ShowLocalAssetInFinder, DeleteLocalAssetAsync));
                }
            }
            this.RaisePropertyChanged(nameof(HasLocalAssets));
        }

        private void ShowLocalAssetInFinder(LocalAssetViewModel asset)
        {
            try
            {
                if (System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows))
                    System.Diagnostics.Process.Start("explorer.exe", $"/select,\"{asset.FilePath}\"");
                else if (System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.OSX))
                    System.Diagnostics.Process.Start("open", $"-R \"{asset.FilePath}\"");
                else
                    System.Diagnostics.Process.Start("xdg-open", Path.GetDirectoryName(asset.FilePath)!);
            }
            catch { }
        }

        private async Task DeleteLocalAssetAsync(LocalAssetViewModel asset)
        {
            var confirmed = await ConfirmDeleteAssetInteraction.Handle(
                $"Delete \"{asset.Label}\"? This cannot be undone.");
            if (!confirmed) return;

            try
            {
                if (File.Exists(asset.FilePath))
                    File.Delete(asset.FilePath);
                var resDir = Path.GetDirectoryName(asset.FilePath);
                if (resDir != null && Directory.Exists(resDir) && !Directory.EnumerateFileSystemEntries(resDir).Any())
                    Directory.Delete(resDir);
                var assetDir = resDir != null ? Path.GetDirectoryName(resDir) : null;
                if (assetDir != null && Directory.Exists(assetDir) && !Directory.EnumerateFileSystemEntries(assetDir).Any())
                    Directory.Delete(assetDir);
                ScanLocalAssets();
            }
            catch { }
        }

        private void RefreshInstalledEngines()
        {
            InstalledEngines.Clear();
            foreach (var e in _engineService.ScanInstalledEngines(_engineInstallLocation))
                InstalledEngines.Add(new InstalledEngineViewModel(e, UninstallEngineAsync));
            this.RaisePropertyChanged(nameof(HasInstalledEngines));
        }

        private async Task UninstallEngineAsync(InstalledEngineViewModel engine)
        {
            var confirmed = await ConfirmUninstallEngineInteraction.Handle(
                $"Uninstall engine {engine.Version}? This will delete all files in:\n{engine.InstallPath}");
            if (!confirmed) return;

            await RunBackgroundTask($"Uninstall {engine.Version}", () =>
            {
                Directory.Delete(engine.InstallPath, recursive: true);
                return Task.CompletedTask;
            });
            RefreshInstalledEngines();
        }

        public bool HasInstalledEngines => InstalledEngines.Count > 0;

        private void LoadSettings()
        {
            try
            {
                if (!File.Exists(SettingsPath)) return;
                var json = File.ReadAllText(SettingsPath);
                var doc = JsonDocument.Parse(json);
                if (doc.RootElement.TryGetProperty("DefaultProjectLocation", out var locEl) && locEl.GetString() is string loc && !string.IsNullOrEmpty(loc))
                {
                    // migrate old default that pointed at ~/Documents
                    if (loc == Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments))
                        loc = Models.AppPaths.Projects;
                    _defaultProjectLocation = loc;
                }
                if (doc.RootElement.TryGetProperty("EngineInstallLocation", out var engLocEl) && engLocEl.GetString() is string engLoc && !string.IsNullOrEmpty(engLoc))
                {
                    // migrate old path that pointed at AvailablePackages directly
                    if (engLoc == Models.AppPaths.AvailablePackages)
                        engLoc = Models.AppPaths.Engines;
                    _engineInstallLocation = engLoc;
                }
                if (doc.RootElement.TryGetProperty("SelectedTheme", out var themeEl) && themeEl.GetString() is string theme && !string.IsNullOrEmpty(theme))
                {
                    _selectedTheme = theme;
                    if (Avalonia.Application.Current != null)
                        Avalonia.Application.Current.RequestedThemeVariant =
                            theme == "Light" ? ThemeVariant.Light : ThemeVariant.Dark;
                }
            }
            catch { }
        }

        private void SaveSettings()
        {
            try
            {
                var json = JsonSerializer.Serialize(new { DefaultProjectLocation, EngineInstallLocation, SelectedTheme }, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(SettingsPath, json);
            }
            catch { }
        }

        private void OnConfirmDelete()
        {
            IsDeleteModalOpen = false;
            _deleteTcs?.TrySetResult(true);
            _deleteTcs = null;
        }

        private void OnCancelDelete()
        {
            IsDeleteModalOpen = false;
            _deleteTcs?.TrySetResult(false);
            _deleteTcs = null;
        }

        private async Task LoadProjectsAsync()
        {
            var loadedProjects = await _projectService.LoadProjectsAsync();
            _projects.Edit(innerList =>
            {
                innerList.AddRange(
                    loadedProjects.Select(p => new ProjectViewModel(p, _projectService, _engineService, DeleteProjectInteraction, EnginePickerInteraction, InstalledEngines))
                );
            });
        }
    }
}
