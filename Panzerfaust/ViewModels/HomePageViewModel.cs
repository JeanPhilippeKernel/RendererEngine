using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using Panzerfaust.Service.Engine;
using Panzerfaust.ViewModels.Base;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    public partial class HomePageViewModel : ViewModelBase
    {
        private readonly IEngineService _engineService;
        public HomePageViewModel(IEngineService engineService, IMessenger messenger)
        {
            _engineService = engineService;
        }

        public HomePageViewModel() : this(new EngineService(), new WeakReferenceMessenger()) { }

        [RelayCommand]
        public async Task LaunchExempleProject()
        {
            await _engineService.Start();
        }
    }
}
