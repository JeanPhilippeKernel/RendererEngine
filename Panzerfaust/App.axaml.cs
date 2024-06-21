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
using CommunityToolkit.Mvvm.Messaging;

namespace Panzerfaust
{
    public partial class App : Application
    {
        public override void Initialize()
        {
            AvaloniaXamlLoader.Load(this);
        }

        public override void OnFrameworkInitializationCompleted()
        {
            var locator = new ViewLocator();
            DataTemplates.Add(locator);

            var services = new ServiceCollection();
            ConfigureViewModels(services);
            ConfigureViews(services);
            services.AddSingleton<IMessenger>(WeakReferenceMessenger.Default);

            var provider = services.BuildServiceProvider();

            Ioc.Default.ConfigureServices(provider);

            var vm = Ioc.Default.GetRequiredService<MainViewModel>();

            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = new MainWindow(vm);
            }
            else if (ApplicationLifetime is ISingleViewApplicationLifetime singleViewPlatform)
            {
                singleViewPlatform.MainView = new MainView { DataContext = vm };
            }

            base.OnFrameworkInitializationCompleted();
        }

        [Singleton(typeof(MainViewModel))]
        [Transient(typeof(HomePageViewModel))]
        [Transient(typeof(CustomSplashScreenViewModel))]
        [Transient(typeof(SettingsViewModel))]
        [SuppressMessage("CommunityToolkit.Extensions.DependencyInjection.SourceGenerators.InvalidServiceRegistrationAnalyzer", "TKEXDI0004:Duplicate service type registration")]
        internal static partial void ConfigureViewModels(IServiceCollection services);

        [Singleton(typeof(MainWindow))]
        [Transient(typeof(HomePageView))]
        [Transient(typeof(CustomSplashScreenView))]
        [Transient(typeof(SettingsViewModel))]
        internal static partial void ConfigureViews(IServiceCollection services);
    }
}
