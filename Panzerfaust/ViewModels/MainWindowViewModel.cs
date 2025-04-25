using DynamicData;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
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

        // Field which keep updated with the user inputs in the search bar
        private string searchText = string.Empty;
        public string SearchText
        {
            get => searchText;
            set => this.RaiseAndSetIfChanged(ref searchText, value);
        }

        // Use of DynamicData's SourceList for batch updates later on,
        // reduces UI refreshes
        private readonly SourceList<ProjectViewModel> projects = new();
        public ReadOnlyObservableCollection<ProjectViewModel> FilteredProjects { get; set; }

        public ReactiveCommand<Unit, Unit> CreateProjectCommand { get; }
        public Interaction<ProjectWindowViewModel, ProjectViewModel?> NewProjectDialog { get; } = new();
        public Interaction<MessageBoxWindowViewModel, bool> DeleteProjectInteraction { get; } = new();


        public MainWindowViewModel()
        {
            var filterPredicate = this.WhenAnyValue(x => x.SearchText)
                .Throttle(TimeSpan.FromMilliseconds(300), RxApp.MainThreadScheduler)
                .Select(searchText => CreateFilterPredicate(searchText.Trim()))
                .DistinctUntilChanged();

            // The connect() method links the project list to the filtered view while
            // bind() propagates the changes to FilteredProjects on the UI thread.
            // Subscribe() is required to activate the "pipeline".
            projects.Connect()
                .Filter(filterPredicate)
                .Bind(out var filteredProjects)
                .Subscribe();

            FilteredProjects = filteredProjects;

            RxApp.MainThreadScheduler.Schedule(LoadProjectsAsync);

            CreateProjectCommand = ReactiveCommand.CreateFromTask(OnCreateProjectCommand);

            MessageBus.Current.Listen<(string, ProjectViewModel)>().Subscribe(OnReceiveMessage);
        }

        private static Func<ProjectViewModel, bool> CreateFilterPredicate(string searchTerm)
        {
            return string.IsNullOrWhiteSpace(searchTerm)
                ? _ => true
                : p => p.Name.Contains(searchTerm, StringComparison.OrdinalIgnoreCase);
        }

        private void OnReceiveMessage((string, ProjectViewModel) message)
        {
            var (action, data) = message;

            if (action == Message.DeleteAction)
            { 
                projects.Remove(data);
            }
        }

        private async Task OnCreateProjectCommand()
        {
            var projectViewModel = new ProjectWindowViewModel();
            var result = await NewProjectDialog.Handle(projectViewModel);
            if (result != null)
            {
                result.SetRemovalInteraction(DeleteProjectInteraction);
                projects.Add(result);
            }
        }

        private async void LoadProjectsAsync()
        {
            var projectService = App.Current?.ServiceProvider?.GetService<Service.IProjectService>();
            if (projectService == null) return;

            var loadedProjects = await projectService.LoadProjectsAsync();

            projects.Edit(innerList =>
            {
                innerList.AddRange(
                    loadedProjects.Select(project => new ProjectViewModel(project, DeleteProjectInteraction))
                );
            });
        }
    }
}
