using Avalonia.Controls;
using Avalonia.ReactiveUI;
using Panzerfaust.ViewModels;
using ReactiveUI;

namespace Panzerfaust.Views
{
    internal partial class MainWindow : ReactiveWindow<MainWindowViewModel>
    {
        public MainWindow()
        {        
            InitializeComponent();
            this.WhenActivated(action => action(ViewModel!.NewProjectDialog.RegisterHandler(DialogHandler)));
        }

        private async Task DialogHandler(IInteractionContext<ProjectWindowViewModel, ProjectViewModel?> interactionContext)
        {
            var dialog = new ProjectWindow
            {
                DataContext = interactionContext.Input
            };

            var result = await dialog.ShowDialog<ProjectViewModel?>(this);
            interactionContext.SetOutput(result);
        }
    }
}
