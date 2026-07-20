using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Panzerfaust.ViewModels;
using Panzerfaust.Views;
using Microsoft.Extensions.DependencyInjection;

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
            Models.AppPaths.EnsureDirectories();

            if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                var window = new MainWindow();

                var services = new ServiceCollection();
                services.AddSingleton<Service.IProjectService, Service.ProjectService>();
                services.AddSingleton<Service.IGitHubReleaseService, Service.GitHubReleaseService>();
                services.AddSingleton<Service.IPolyHavenService, Service.PolyHavenService>();
                services.AddSingleton<Service.IStorageProviderService>(_ => new Service.StorageProviderService(window));
                services.AddSingleton<Service.IEngineService, Service.EngineService>();
                services.AddTransient<ProjectWindowViewModel>();
                services.AddTransient<Func<ProjectWindowViewModel>>(sp => () =>
                {
                    var mainVm = sp.GetRequiredService<MainWindowViewModel>();
                    return new ProjectWindowViewModel(
                        sp.GetRequiredService<Service.IStorageProviderService>(),
                        sp.GetRequiredService<Service.IProjectService>(),
                        sp.GetRequiredService<Service.IEngineService>(),
                        mainVm.DefaultProjectLocation);
                });
                services.AddTransient<MainWindowViewModel>();
                ServiceProvider = services.BuildServiceProvider();

                window.DataContext = ServiceProvider.GetRequiredService<MainWindowViewModel>();
                desktop.MainWindow = window;
                window.Show();
            }

            base.OnFrameworkInitializationCompleted();
        }
    }
}
