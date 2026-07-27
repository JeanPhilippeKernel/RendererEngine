using Panzerfaust.Models;
using Panzerfaust.Service;
using ReactiveUI;
using System;
using System.IO;
using System.Reactive;
using System.Reactive.Linq;
using System.Reactive.Threading.Tasks;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class ProjectViewModel : ViewModelBase
    {
        private readonly Project _project;
        private readonly IProjectService _projectService;
        private readonly IEngineService _engineService;
        private Interaction<string, bool>? _deleteProjectInteraction;
        private Interaction<EnginePickerViewModel, InstalledEngineViewModel?>? _enginePickerInteraction;
        private System.Collections.ObjectModel.ObservableCollection<InstalledEngineViewModel>? _installedEngines;

        public string Name => _project.Name;
        public string Path => _project.Fullpath;
        public string UpdatedDate => _project.UpdateDate.ToShortDateString();
        public string CreatedDate => _project.CreationDate.ToShortDateString();
        public string ProjectSize => GetProjectSize();

        public string? ThumbnailPath
        {
            get
            {
                var thumb = System.IO.Path.Combine(_project.Fullpath, "thumbnail.png");
                return System.IO.File.Exists(thumb) ? thumb : null;
            }
        }

        public ReactiveCommand<Unit, Unit> OpenProjectCommand { get; }
        public ReactiveCommand<Unit, Unit> DeleteProjectCommand { get; }
        public ReactiveCommand<Unit, Unit> ShowInfoCommand { get; }

        public ProjectViewModel(Project p, IProjectService projectService, IEngineService engineService,
            Interaction<string, bool>? deleteInteraction = null,
            Interaction<EnginePickerViewModel, InstalledEngineViewModel?>? enginePickerInteraction = null,
            System.Collections.ObjectModel.ObservableCollection<InstalledEngineViewModel>? installedEngines = null)
        {
            _project = p;
            _projectService = projectService;
            _engineService = engineService;
            _deleteProjectInteraction = deleteInteraction;
            _enginePickerInteraction = enginePickerInteraction;
            _installedEngines = installedEngines;
            OpenProjectCommand = ReactiveCommand.CreateFromTask(OnOpenProjectCommand);
            OpenProjectCommand.ThrownExceptions.Subscribe(_ => { });
            DeleteProjectCommand = ReactiveCommand.CreateFromTask(OnDeleteProjectCommand);
            ShowInfoCommand = ReactiveCommand.Create(() =>
                MessageBus.Current.SendMessage<(string, ProjectViewModel)>((Message.ShowInfoAction, this)));
        }

        public void SetRemovalInteraction(Interaction<string, bool> interaction) => _deleteProjectInteraction = interaction;

        public void SetEnginePickerInteraction(
            Interaction<EnginePickerViewModel, InstalledEngineViewModel?> interaction,
            System.Collections.ObjectModel.ObservableCollection<InstalledEngineViewModel> engines)
        {
            _enginePickerInteraction = interaction;
            _installedEngines = engines;
        }

        private async Task OnDeleteProjectCommand()
        {
            if (_deleteProjectInteraction == null) { return; }

            var result = await _deleteProjectInteraction.Handle(_project.Name).ToTask();
            if (result)
            {
                await _projectService.DeleteAsync(_project);
                MessageBus.Current.SendMessage<(string, ProjectViewModel)>((Message.DeleteAction, this));
            }
        }

        private async Task OnOpenProjectCommand()
        {
            try
            {
                if (_enginePickerInteraction != null && _installedEngines != null)
                {
                    var pickerVm = new EnginePickerViewModel(_project.Name, _installedEngines);
                    var chosen = await _enginePickerInteraction.Handle(pickerVm).ToTask();
                    if (chosen == null) return; // user cancelled
                    await _engineService.StartAsync(_project.Fullpath, chosen.BinaryPath).ConfigureAwait(false);
                }
                else
                {
                    await _engineService.StartAsync(_project.Fullpath).ConfigureAwait(false);
                }
            }
            catch (Exception ex)
            {
                MessageBus.Current.SendMessage<(string, string)>((Message.ToastErrorAction, $"Failed to open project: {ex.Message}"));
            }
        }

        private string GetProjectSize()
        {
            try
            {
                if (!Directory.Exists(_project.Fullpath)) return "N/A";
                long bytes = 0;
                foreach (var f in new DirectoryInfo(_project.Fullpath).EnumerateFiles("*", SearchOption.AllDirectories))
                    bytes += f.Length;
                return bytes switch
                {
                    < 1024 => $"{bytes} B",
                    < 1024 * 1024 => $"{bytes / 1024.0:F1} KB",
                    < 1024 * 1024 * 1024 => $"{bytes / (1024.0 * 1024):F1} MB",
                    _ => $"{bytes / (1024.0 * 1024 * 1024):F1} GB"
                };
            }
            catch
            {
                return "N/A";
            }
        }
    }
}
