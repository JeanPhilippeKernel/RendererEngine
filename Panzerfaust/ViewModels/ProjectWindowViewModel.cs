using Panzerfaust.Service;
using ReactiveUI;
using System.ComponentModel;
using System.Reactive;
using System.Reactive.Linq;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Panzerfaust.ViewModels
{
    internal partial class ProjectWindowViewModel : ViewModelBase
    {
        private readonly IStorageProviderService _storageService;
        private readonly IProjectService _projectService;
        private readonly IEngineService _engineService;
        private bool _isBusy = false;
        private string?  _projectName = string.Empty;
        private string? _projectLocation;
        private string? _progressReportText = string.Empty;
        private string _progressReportTextColor = "White";

        public bool IsBusy { get => _isBusy; set => this.RaiseAndSetIfChanged(ref _isBusy, value); }
        public string? ProjectName
        { 
            get => _projectName;
            set
            {
                ValidateProjectName(value);
                this.RaiseAndSetIfChanged(ref _projectName, value);
            }  
        }

        public string? ProjectLocation
        {
            get => _projectLocation;
            set
            {
                ValidateProjectLocation(value);
                this.RaiseAndSetIfChanged(ref _projectLocation, value);
            }
        }

        public string? ProgressReportText { get => _progressReportText; set => this.RaiseAndSetIfChanged(ref _progressReportText, value); }
        public string ProgressReportTextColor { get => _progressReportTextColor; set => this.RaiseAndSetIfChanged(ref _progressReportTextColor, value); }

        public ICommand DirectoryDialogCommand { get; }
        public ReactiveCommand<Unit, ProjectViewModel?> CancelCommand { get; }
        public ReactiveCommand<Unit, ProjectViewModel?> FinishCommand { get; }

        public ProjectWindowViewModel(IStorageProviderService storageService, IProjectService projectService, IEngineService engineService, string? defaultLocation = null)
        {
            _storageService = storageService;
            _projectService = projectService;
            _engineService = engineService;
            _projectLocation = defaultLocation ?? Directory.GetCurrentDirectory();
            DirectoryDialogCommand = ReactiveCommand.CreateFromTask(OnDirectoryDialogCommandHandler);
            CancelCommand = ReactiveCommand.Create(OnCancelCommandHandler);
            FinishCommand = ReactiveCommand.CreateFromTask(OnFinishCommandHandler,
                this.WhenAnyValue(x => x.ProjectName, x => x.ProjectLocation, x => x.HasErrors,  (name, location, hasErrors) => !(string.IsNullOrEmpty(name) || string.IsNullOrEmpty(location) || hasErrors)));
        }

        private async Task OnDirectoryDialogCommandHandler()
        {
            var folder = await _storageService.PickDirectoryAsync();
            ProjectLocation = folder?.Path.LocalPath;
        }

        private ProjectViewModel? OnCancelCommandHandler() { return null; }

        private async Task<ProjectViewModel?> OnFinishCommandHandler()
        {
            IsBusy = true;
            ProgressReportTextColor = "White";

            var (success, result, reportMessage) = await ConfigureAsync();
            IsBusy = false;
            
            if(reportMessage != null) ProgressReportText = reportMessage;

            if(!success) ProgressReportTextColor = "Red";

            return result;
        }

        private async Task<(bool, ProjectViewModel?, string?)> ConfigureAsync()
        {
            string fullpath = Path.Combine(ProjectLocation!, ProjectName!);

            bool directoryExist = await _storageService.IsDirectoryExists(fullpath);
            if (directoryExist)
            {
                return (false, null, "The directory already exists");
            }

            // Creating Root directory...
            var rootDirectory = await _storageService.CreateDirectoryAsync(fullpath);
            if (!rootDirectory.Exists)
            {
                return (false, null, "Failed to create the directory");
            }

            // Creating projectConfig.json...
            Models.ProjectConfigJson content = new()
            {
                ProjectName = ProjectName!,
                DefautImportDirectory = new() { TextureDirectory = "Textures", SoundDirectory = "Sounds" }
            };

            ProgressReportText = "Creating config json file...";
            var (fileCreated, fileStream) = await _storageService.CreateFileAsync(Path.Combine(rootDirectory.FullName, "projectConfig.json"));
            if (!fileCreated)
            {
                return (false, null, "Failed to create projectConfig.json");
            }

            if (fileStream != null)
            {
                using StreamWriter writer = new(fileStream);
                var jsonContent = content.ToJson();
                await writer.WriteAsync(jsonContent);
            }

            // Creating sub directories...            
            List<string> subDirectories = new()
            {
              content.SceneDataDirectory,
              content.SceneDirectory,
              content.DefautImportDirectory.SoundDirectory,
              content.DefautImportDirectory.TextureDirectory
            };

            foreach (var directory in subDirectories)
            {
                ProgressReportText = $"Creating {directory} directory...";
                var _ = rootDirectory.CreateSubdirectory(directory);
                if (!_.Exists)
                {
                    return (false, null, $"Failed to create directory: {directory}");
                }
            }

            // Creating project...
            var project = await _projectService.CreateAsync(ProjectName!, rootDirectory.FullName, rootDirectory.CreationTime, rootDirectory.LastAccessTime);
            if (project == null)
            {
                return (false, null, "Failed to create project");
            }

            return (true, new ProjectViewModel(project, _projectService, _engineService), "Configuration completed!");
        }
    }
}
