using System;
using System.IO;

namespace Panzerfaust.Models
{
    public static class AppPaths
    {
        private static readonly string Root =
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Panzerfaust");

        public static string AppSettings        => Path.Combine(Root, "AppSettings");
        public static string Projects           => Path.Combine(Root, "projects");
        public static string AvailablePackages  => Path.Combine(Root, "AvailablePackages");
        public static string Engines            => Path.Combine(AvailablePackages, "engines");
        public static string Plugins            => Path.Combine(AvailablePackages, "plugins");
        public static string Assets             => Path.Combine(AvailablePackages, "assets");

        public static string SettingsFile        => Path.Combine(AppSettings, "settings.json");
        public static string DownloadHistoryFile => Path.Combine(AppSettings, "download_history.json");

        public static void EnsureDirectories()
        {
            Directory.CreateDirectory(AppSettings);
            Directory.CreateDirectory(Projects);
            Directory.CreateDirectory(Engines);
            Directory.CreateDirectory(Plugins);
            Directory.CreateDirectory(Assets);
        }
    }
}
