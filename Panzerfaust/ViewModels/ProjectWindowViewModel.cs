using Avalonia;
using Microsoft.Extensions.DependencyInjection;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reactive;
using System.Reactive.Linq;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Panzerfaust.ViewModels
{
    internal class ProjectWindowViewModel : ViewModelBase
    {
        private bool _isBusy = false;
        private string?  _projectName = string.Empty;
        private string? _projectLocation = string.Empty;
        private string? _progressReportText = string.Empty;

        public bool IsBusy { get => _isBusy; set => this.RaiseAndSetIfChanged(ref _isBusy, value); }
        public string? ProjectName { get => _projectName; set => this.RaiseAndSetIfChanged(ref _projectName, value); }
        public string? ProjectLocation { get => _projectLocation; set => this.RaiseAndSetIfChanged(ref _projectLocation, value); }
        public string? ProgressReportText { get => _progressReportText; set => this.RaiseAndSetIfChanged(ref _progressReportText, value); }

        public ICommand DirectoryDialogCommand { get; }
        public ReactiveCommand<Unit, ProjectViewModel?> CancelCommand { get; }
        public ReactiveCommand<Unit, ProjectViewModel?> FinishCommand { get; } 

        public ProjectWindowViewModel()
        {
            CancelCommand = ReactiveCommand.Create(OnCancelCommandHandler);
            FinishCommand = ReactiveCommand.CreateFromTask(OnFinishCommandHandler);
            DirectoryDialogCommand = ReactiveCommand.CreateFromTask(OnDirectoryDialogCommandHandler);
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

            var (success, result, reportMessage) = await ConfigureAsync();
            IsBusy = false;
            
            if(reportMessage != null) ProgressReportText = reportMessage;

            if (success)
            {

            }
            return result;
        }

        private async Task<(bool, ProjectViewModel?, string?)> ConfigureAsync()
        {
            var storageProvider = App.Current?.ServiceProvider?.GetService<Service.IStorageProviderService>();
            if (storageProvider == null)
            {
                return (false, null, "Failed to access the Storage Provider");
            }

            string fullpath = Path.Combine(_projectLocation, _projectName);

            bool directoryExist = await storageProvider.IsDirectoryExists(fullpath);
            if (directoryExist)
            {
                return (false, null, "The directory already exists");
            }

            // Creating Root directory
            var rootDirectory = await storageProvider.CreateDirectoryAsync(fullpath);
            if (!rootDirectory.Exists)
            {
                return (false, null, "Failed to create the directory");
            }

            // Creating projectConfig.json
            Models.ProjectConfigJson content = new()
            {
                ProjectName = _projectName,
                DefautImportDirectory = new() { TextureDirectory = "Textures", SoundDirectory = "Sounds" }
            };

            ProgressReportText = "Creating config json file...";
            var (fileCreated, fileStream) = await storageProvider.CreateFileAsync($"{rootDirectory.FullName}/projectConfig.json");
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

            // Creating sub directories            
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

            Models.Project project = new() { Name = _projectName, CreationDate = rootDirectory.CreationTime, UpdateDate = rootDirectory.LastAccessTime, Fullpath = rootDirectory.FullName };
            return (true, new ProjectViewModel (project), "Configuration completed!");
        }
    }
}
