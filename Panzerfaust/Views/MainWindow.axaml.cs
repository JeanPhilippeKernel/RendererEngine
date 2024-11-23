using Avalonia.Controls;
using Avalonia.ReactiveUI;
using Panzerfaust.ViewModels;
using ReactiveUI;
using System.Reactive;

namespace Panzerfaust.Views
{
    internal partial class MainWindow : ReactiveWindow<MainWindowViewModel>
    {
        public MainWindow()
        {
            InitializeComponent();
            this.WhenActivated(
                action =>
                {
                    ViewModel!.NewProjectDialog.RegisterHandler(DialogHandler);
                    ViewModel!.DeleteProjectInteraction.RegisterHandler(DeleteProjectInteractionHandler);
                });
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

        private async Task DeleteProjectInteractionHandler(IInteractionContext<MessageBoxWindowViewModel, bool> context)
        {
            var dialog = new MessageBoxWindow { DataContext = context.Input};
            var result = await dialog.ShowDialog<bool>(this);
            context.SetOutput(result);
        }
    }
}
