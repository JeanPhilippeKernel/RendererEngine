using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reactive;
using System.Reactive.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class ProjectViewModel : ViewModelBase
    {
        private readonly Project _project;
        private Interaction<MessageBoxWindowViewModel, bool>? _deleteProjectInteraction;

        public string Name => _project.Name;
        public string Path => _project.Fullpath;
        public string UpdatedDate => _project.UpdateDate.ToShortDateString();

        public ReactiveCommand<Unit, Unit> OpenProjectCommand { get; }
        public ReactiveCommand<Unit, Unit> DeleteProjectCommand { get; }

        public ProjectViewModel(Project p, Interaction<MessageBoxWindowViewModel, bool>? interaction = null)
        {
            _project = p;
            _deleteProjectInteraction = interaction;
            OpenProjectCommand = ReactiveCommand.Create(OnOpenProjectCommand);
            DeleteProjectCommand = ReactiveCommand.CreateFromTask(OnDeleteProjectCommand);
        }

        public void SetRemovalInteraction(Interaction<MessageBoxWindowViewModel, bool> interaction) => _deleteProjectInteraction = interaction;

        private async Task OnDeleteProjectCommand()
        {
            if (_deleteProjectInteraction == null) { return; }

            var result = await _deleteProjectInteraction.Handle(new MessageBoxWindowViewModel());
            if (result)
            {
                var projectService = App.Current?.ServiceProvider?.GetService<Service.IProjectService>();
                if (projectService == null) { return; }

                await projectService.DeleteAsync(_project);
                MessageBus.Current.SendMessage<(string, ProjectViewModel)>((Message.DeleteAction, this));
            }
        }

        private async void OnOpenProjectCommand()
        {
            var engineService = App.Current?.ServiceProvider?.GetService<Service.IEngineService>();
            if (engineService == null) { return; }

            await engineService.StartAsync(_project.Fullpath).ConfigureAwait(false);
        }
    }
}
