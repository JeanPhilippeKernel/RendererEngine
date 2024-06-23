using CommunityToolkit.Mvvm.ComponentModel;
using Panzerfaust.ViewModels.Base;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    public partial class CustomSplashScreenViewModel : ViewModelBase
    {
        [ObservableProperty]
        private string _startupMessage = "Starting application...";

        public void Cancel()
        {
            StartupMessage = "Cancelling...";
            _cts.Cancel();
        }

        private readonly CancellationTokenSource _cts = new();

        public CancellationToken CancellationToken => _cts.Token;
    }
}
