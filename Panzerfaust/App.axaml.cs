using System;
using System.Diagnostics.CodeAnalysis;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Panzerfaust.ViewModels;
using Panzerfaust.Views;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Extensions;

namespace Panzerfaust
{
    public partial class App : Application
    {
        public IServiceProvider? ServiceProvider { get; private set; }
        public new static App? Current => Application.Current as App;

        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = new MainWindow() { };

                var services = new ServiceCollection();
                services.AddSingleton<Service.IProjectService>(new Service.ProjectService());
                services.AddSingleton<Service.IStorageProviderService>(x => new Service.StorageProviderService(desktop.MainWindow));
                services.AddSingleton<Service.IEngineService>(new Service.EngineService());
                ServiceProvider = services.BuildServiceProvider();

                MainWindow window = (MainWindow)desktop.MainWindow;
                window.DataContext = new MainWindowViewModel();
                 
                desktop.MainWindow.Show();
            }

            base.OnFrameworkInitializationCompleted();
        }
    }
}
