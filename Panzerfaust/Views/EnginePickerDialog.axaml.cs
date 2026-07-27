using Avalonia.Controls;
using Avalonia.Interactivity;
using Panzerfaust.ViewModels;

namespace Panzerfaust.Views
{
    internal partial class EnginePickerDialog : Window
    {
        public EnginePickerDialog()
        {
            InitializeComponent();
        }

        private void OnOpen(object? sender, RoutedEventArgs e)
            => Close((DataContext as EnginePickerViewModel)?.SelectedEngine);

        private void OnCancel(object? sender, RoutedEventArgs e)
            => Close(null);
    }
}
