using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace Panzerfaust.ViewModels
{
    internal class MainWindowViewModel : ViewModelBase
    {
        public ICommand CreateProjectCommand { get; }
        public ObservableCollection<ProjectViewModel> Projects { get; set; } = new();
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
            return;
        }
    }
}
