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
        private static int[] BuildKMPTable(string pattern)
        {
            int j = 0; // Position in the pattern

            int m = pattern.Length;
            int[] lps = new int[m]; // longest prefix which is also a suffix

            lps[0] = 0;
            for (int i = 1; i < m; i++)
            {
                while (j > 0 && char.ToLower(pattern[i]) != char.ToLower(pattern[j]))
                {
                    j = lps[j - 1];
                }
                if (char.ToLower(pattern[i]) == char.ToLower(pattern[j]))
                {
                    j++;
                }
                lps[i] = j;
            }
            return lps;
        }

        private static bool KMPSearch(string searchText, string pattern)
        {

            int j;
            int[] lps;
            
            // Edge cases
            if (string.IsNullOrEmpty(pattern))
                return true;

            if (string.IsNullOrEmpty(searchText))
                return false;

            if (pattern.Length > searchText.Length)
                return false;

            j = 0;
            lps = BuildKMPTable(pattern);

            for (int i = 0; i < searchText.Length; i++)
            {
                while (j > 0 && char.ToLower(searchText[i]) != char.ToLower(pattern[j]))
                {
                    j = lps[j - 1];
                }
                if (char.ToLower(searchText[i]) == char.ToLower(pattern[j]))
                {
                    j++;
                }
                if (j == pattern.Length)
                {
                    return true;
                }
            }

            return false;
        }

        private Func<ProjectViewModel, bool> CreateFilterPredicate(string searchTerm)
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
