using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Avalonia.ReactiveUI;
using DynamicData.Kernel;
using Panzerfaust.ViewModels;
using ReactiveUI;

namespace Panzerfaust.Views
{ 
    internal partial class ProjectWindow : ReactiveWindow<ProjectWindowViewModel>
    {
        public ProjectWindow()
        {
            InitializeComponent();
            this.WhenActivated(action =>
            {
                action(ViewModel!.CancelCommand.Subscribe(Close));
                action(ViewModel!.FinishCommand.Subscribe(_ => { if (_ != null) { Close(_); } }));
            });
        }
    }
}