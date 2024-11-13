using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Models;
using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reactive;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class ProjectViewModel : ViewModelBase
    {
        private readonly Project _project;

        public string Name => _project.Name;
        public string Path => _project.Fullpath;
        public string UpdatedDate => _project.UpdateDate.ToShortDateString();

        public ReactiveCommand<Unit, Unit> OpenProjectCommand { get; }

        public ProjectViewModel(Project p)
        {
            _project = p;
            OpenProjectCommand = ReactiveCommand.Create(OnOpenProjectCommand);
        }

        private async void OnOpenProjectCommand()
        {
            var engineService = App.Current?.ServiceProvider?.GetService<Service.IEngineService>();
            if (engineService == null) { return; }

            await engineService.StartAsync(_project.Fullpath).ConfigureAwait(false);
        }
    }
}
