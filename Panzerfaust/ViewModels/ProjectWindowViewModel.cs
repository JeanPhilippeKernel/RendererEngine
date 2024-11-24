using Avalonia;
using Microsoft.Extensions.DependencyInjection;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Reactive;
using System.Reactive.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Panzerfaust.ViewModels
{
    internal partial class ProjectWindowViewModel : ViewModelBase
    {
        private bool _isBusy = false;
        private string?  _projectName = string.Empty;
        private string? _projectLocation = Directory.GetCurrentDirectory();
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

        public ProjectWindowViewModel()
        {
            DirectoryDialogCommand = ReactiveCommand.CreateFromTask(OnDirectoryDialogCommandHandler);
            CancelCommand = ReactiveCommand.Create(OnCancelCommandHandler);
            FinishCommand = ReactiveCommand.CreateFromTask(OnFinishCommandHandler,
                this.WhenAnyValue(x => x.ProjectName, x => x.ProjectLocation, x => x.HasErrors,  (name, location, hasErrors) => !(string.IsNullOrEmpty(name) || string.IsNullOrEmpty(location) || hasErrors)));
        }

        private async Task OnDirectoryDialogCommandHandler()
        {
            var storageProvider = App.Current?.ServiceProvider?.GetService<Service.IStorageProviderService>();
            if (storageProvider != null)
            { 
                var folder = await storageProvider.PickDirectoryAsync();
                ProjectLocation = folder?.Path.LocalPath;
            }
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
            var storageService = App.Current?.ServiceProvider?.GetService<Service.IStorageProviderService>();

            if (storageService == null)
            {
                return (false, null, "Failed to access the Storage Provider");
            }

            string fullpath = Path.Combine(ProjectLocation, ProjectName);

            bool directoryExist = await storageService.IsDirectoryExists(fullpath);
            if (directoryExist)
            {
                return (false, null, "The directory already exists");
            }

            // Creating Root directory...
            var rootDirectory = await storageService.CreateDirectoryAsync(fullpath);
            if (!rootDirectory.Exists)
            {
                return (false, null, "Failed to create the directory");
            }

            // Creating projectConfig.json...
            Models.ProjectConfigJson content = new()
            {
                ProjectName = ProjectName,
                DefautImportDirectory = new() { TextureDirectory = "Textures", SoundDirectory = "Sounds" }
            };

            ProgressReportText = "Creating config json file...";
            var (fileCreated, fileStream) = await storageService.CreateFileAsync($"{rootDirectory.FullName}/projectConfig.json");
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
            var projectService = App.Current?.ServiceProvider?.GetService<Service.IProjectService>();
            if (projectService == null)
            {
                return (false, null, "Failed to access the Project Service");
            }

            var project = await projectService.CreateAsync(ProjectName, rootDirectory.FullName, rootDirectory.CreationTime, rootDirectory.LastAccessTime);
            if (project == null)
            {
                return (false, null, "Failed to create project");
            }

            return (true, new ProjectViewModel (project), "Configuration completed!");
        }
    }
}
