using Avalonia.Controls;
using Panzerfaust.ViewModels;

namespace Panzerfaust.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow(MainViewModel vm)
        {
            DataContext = vm;
            InitializeComponent();
        }

        public MainWindow() : this(new MainViewModel()) { }


    }
}
