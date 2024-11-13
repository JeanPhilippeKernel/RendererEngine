using CommunityToolkit.Mvvm.Messaging;
using Microsoft.Extensions.DependencyInjection;
using Panzerfaust.Service;
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
        }
    }
}
