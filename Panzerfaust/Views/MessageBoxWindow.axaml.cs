using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.ReactiveUI;
using Panzerfaust.ViewModels;
using ReactiveUI;

namespace Panzerfaust.Views;

internal partial class MessageBoxWindow : ReactiveWindow<MessageBoxWindowViewModel>
{
    public MessageBoxWindow()
    {
        InitializeComponent();
        this.WhenActivated(action =>
        {
            action(ViewModel!.CancelCommand.Subscribe(_ => Close(_)));
            action(ViewModel!.OkCommand.Subscribe(_ => Close(_)));
        });
    }
}