using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Service.Engine;
using Panzerfaust.ViewModels;
using Panzerfaust.Views;

namespace Panzerfaust.Extensions
{
    public static class ServiceCollectionExtensions
    {
        public static void AddCommonServices(this IServiceCollection collection)
        {
            //Services
            collection.AddSingleton<IMessenger>(WeakReferenceMessenger.Default);
            collection.AddSingleton<IEngineService, EngineService>();

            //ViewModels
            collection.AddTransient<CustomSplashScreenViewModel>();
            collection.AddSingleton<MainViewModel>();
            collection.AddTransient<HomePageViewModel>();
            collection.AddTransient<SettingsViewModel>();

            // Views
            collection.AddTransient<CustomSplashScreenView>();
            collection.AddSingleton<MainView>();
            collection.AddTransient<HomePageView>();
            collection.AddTransient<SettingsView>();
        }
    }
}
