using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.ReactiveUI;
using Panzerfaust.ViewModels;
using ReactiveUI;
using System;
using System.Reactive.Linq;
using System.Threading.Tasks;

namespace Panzerfaust.Views
{
    internal partial class MainWindow : ReactiveWindow<MainWindowViewModel>
    {
        public MainWindow()
        {
            InitializeComponent();
            this.WhenActivated(action =>
            {
                action(ViewModel!.NewProjectDialog.RegisterHandler(DialogHandler));
                action(ViewModel!.ConfirmOverwriteInteraction.RegisterHandler(ConfirmOverwriteHandler));
                action(ViewModel!.EnginePickerInteraction.RegisterHandler(EnginePickerHandler));
                action(ViewModel!.ConfirmDeleteAssetInteraction.RegisterHandler(ConfirmDeleteAssetHandler));
                action(ViewModel!.ConfirmUninstallEngineInteraction.RegisterHandler(ConfirmUninstallEngineHandler));

                action(ViewModel!.WhenAnyValue(x => x.IsDeleteModalOpen).Subscribe(open =>
                    RootContent.Effect = open ? new BlurEffect { Radius = 8 } : null));
            });
        }

        private async Task<T> ShowBlurred<T>(Func<Task<T>> showDialog)
        {
            RootContent.Effect = new BlurEffect { Radius = 8 };
            try { return await showDialog(); }
            finally { RootContent.Effect = null; }
        }

        private async Task DialogHandler(IInteractionContext<ProjectWindowViewModel, ProjectViewModel?> interactionContext)
        {
            var dialog = new ProjectWindow { DataContext = interactionContext.Input };
            var result = await ShowBlurred(() => dialog.ShowDialog<ProjectViewModel?>(this));
            interactionContext.SetOutput(result);
        }

        private async Task ConfirmOverwriteHandler(IInteractionContext<string, bool> interactionContext)
        {
            var dialog = new ConfirmDialog { Message = interactionContext.Input, ConfirmLabel = "Overwrite" };
            var result = await ShowBlurred(() => dialog.ShowDialog<bool>(this));
            interactionContext.SetOutput(result);
        }

        private async Task EnginePickerHandler(IInteractionContext<EnginePickerViewModel, InstalledEngineViewModel?> interactionContext)
        {
            var dialog = new EnginePickerDialog { DataContext = interactionContext.Input };
            var result = await ShowBlurred(() => dialog.ShowDialog<InstalledEngineViewModel?>(this));
            interactionContext.SetOutput(result);
        }

        private async Task ConfirmDeleteAssetHandler(IInteractionContext<string, bool> interactionContext)
        {
            var dialog = new ConfirmDialog { Message = interactionContext.Input, ConfirmLabel = "Delete" };
            var result = await ShowBlurred(() => dialog.ShowDialog<bool>(this));
            interactionContext.SetOutput(result);
        }

        private async Task ConfirmUninstallEngineHandler(IInteractionContext<string, bool> interactionContext)
        {
            var dialog = new ConfirmDialog { Message = interactionContext.Input, ConfirmLabel = "Uninstall" };
            var result = await ShowBlurred(() => dialog.ShowDialog<bool>(this));
            interactionContext.SetOutput(result);
        }
    }
}
