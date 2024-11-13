using DynamicData;
using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Reactive;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;
using static SkiaSharp.HarfBuzz.SKShaper;

namespace Panzerfaust.ViewModels
{
    internal class MainWindowViewModel : ViewModelBase
    {
        public ObservableCollection<ProjectViewModel> Projects { get; set; } = new();
        public ReactiveCommand<Unit, Unit> CreateProjectCommand { get; }
        public Interaction<ProjectWindowViewModel, ProjectViewModel?> NewProjectDialog { get; } = new();

        public MainWindowViewModel()
        {
            RxApp.MainThreadScheduler.Schedule(LoadProjectsAsync);

            CreateProjectCommand = ReactiveCommand.CreateFromTask(OnCreateProjectCommand);
        }

        private async Task OnCreateProjectCommand()
        {
            var projectViewModel = new ProjectWindowViewModel();
            var result = await NewProjectDialog.Handle(projectViewModel);
            if (result != null) 
            {
                Projects.Add(result);
            }
        }

        private async void LoadProjectsAsync()
        {
            var projectService = App.Current?.ServiceProvider?.GetService<Service.IProjectService>();
            if (projectService == null) return;

            var projects = await projectService.LoadProjectsAsync();
            Projects.AddRange(projects.Select(project => new ProjectViewModel(project)));
        }
    }
}
