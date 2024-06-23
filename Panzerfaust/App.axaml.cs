using System;
using System.Diagnostics.CodeAnalysis;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Panzerfaust.ViewModels;
using Panzerfaust.Views;
using CommunityToolkit.Extensions.DependencyInjection;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Extensions;
using CommunityToolkit.Mvvm.Messaging;
using Panzerfaust.Service.AppEnvironment;
using Panzerfaust.Service.Engine;

namespace Panzerfaust
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override async void OnFrameworkInitializationCompleted()
        {
            var locator = new ViewLocator();
            DataTemplates.Add(locator);

            var services = new ServiceCollection();
            services.AddCommonServices();

            var provider = services.BuildServiceProvider();

            Ioc.Default.ConfigureServices(provider);

            var vm = Ioc.Default.GetRequiredService<MainViewModel>();


            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                var splashScreenVM = new CustomSplashScreenViewModel();
                var splashScreen = new CustomSplashScreenView
                {
                    DataContext = splashScreenVM
                };

                desktop.MainWindow = splashScreen;

                splashScreen.Show();

                try
                {
                    splashScreenVM.StartupMessage = "Searching for devices...";
                    await Task.Delay(1000, splashScreenVM.CancellationToken);
                    splashScreenVM.StartupMessage = "Connecting to device ...";
                    await Task.Delay(2000, splashScreenVM.CancellationToken);
                    splashScreenVM.StartupMessage = "Configuring device...";
                    await Task.Delay(2000, splashScreenVM.CancellationToken);
                }
                catch (TaskCanceledException)
                {
                    splashScreen.Close();
                    return;
                }

                var mainWin = new MainWindow
                {
                    DataContext = new MainViewModel(),
                };
                desktop.MainWindow = mainWin;
                mainWin.Show();

                splashScreen.Close();
            }

            base.OnFrameworkInitializationCompleted();

        }
    }
}
