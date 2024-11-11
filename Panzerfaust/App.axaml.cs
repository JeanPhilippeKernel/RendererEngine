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
            var locator = new ViewLocator();
            DataTemplates.Add(locator);


            //services.AddCommonServices();
            //var provider = services.BuildServiceProvider();

            //Ioc.Default.ConfigureServices(provider);

            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = new MainWindow() { DataContext = new MainWindowViewModel() };

                var services = new ServiceCollection();
                services.AddSingleton<Service.IStorageProviderService>(x => new Service.StorageProviderService(desktop.MainWindow));
                ServiceProvider = services.BuildServiceProvider();

                desktop.MainWindow.Show();
            }

            base.OnFrameworkInitializationCompleted();

        }
    }
}
