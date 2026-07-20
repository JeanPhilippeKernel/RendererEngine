using Avalonia.Controls;
using Avalonia.Interactivity;

namespace Panzerfaust.Views
{
    internal partial class ConfirmDialog : Window
    {
        public string Message
        {
            get => (DataContext as ConfirmDialogModel)?.Message ?? string.Empty;
            set
            {
                var model = DataContext as ConfirmDialogModel ?? new ConfirmDialogModel();
                model.Message = value;
                DataContext = model;
            }
        }

        public string ConfirmLabel
        {
            get => (DataContext as ConfirmDialogModel)?.ConfirmLabel ?? "Confirm";
            set
            {
                var model = DataContext as ConfirmDialogModel ?? new ConfirmDialogModel();
                model.ConfirmLabel = value;
                DataContext = model;
            }
        }

        public ConfirmDialog()
        {
            InitializeComponent();
        }

        private void OnConfirm(object? sender, RoutedEventArgs e) => Close(true);
        private void OnCancel(object? sender, RoutedEventArgs e) => Close(false);
    }

    internal class ConfirmDialogModel
    {
        public string Message { get; set; } = string.Empty;
        public string ConfirmLabel { get; set; } = "Confirm";
    }
}
