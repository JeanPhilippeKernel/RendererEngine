using Panzerfaust.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public class EngineService : IEngineService
    {
        string _enginePath = string.Empty;
        string _workingDirectory = string.Empty;

        const string _launcherCLIAppName = "Obelisk";
        const string _configJsonFilename = "projectConfig.json";
        const string _projectFileCommandLineArgs = "--projectConfigFile";
        const string _launchEditorFlag = "--launchEditor";
        const string _launchEditorValue = "1";

        static string EngineExtension =>
            RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? ".exe" : string.Empty;

        public EngineService()
        {
            var appDir = Path.GetDirectoryName(AppContext.BaseDirectory) ?? Environment.CurrentDirectory;
            _enginePath = Path.Combine(appDir, $"{_launcherCLIAppName}{EngineExtension}");
            _workingDirectory = appDir;
        }

        public IEnumerable<InstalledEngine> ScanInstalledEngines(string installLocation)
        {
            if (!Directory.Exists(installLocation))
                yield break;

            var binaryName = $"{_launcherCLIAppName}{EngineExtension}";

            foreach (var versionDir in Directory.EnumerateDirectories(installLocation))
            {
                var binaryPath = Directory
                    .EnumerateFiles(versionDir, binaryName, SearchOption.AllDirectories)
                    .FirstOrDefault();

                if (binaryPath != null)
                    yield return new InstalledEngine
                    {
                        Version = Path.GetFileName(versionDir),
                        BinaryPath = binaryPath,
                        InstallPath = versionDir
                    };
            }
        }

        public Task StartAsync(string projectPath) => StartAsync(projectPath, _enginePath);

        public async Task StartAsync(string projectPath, string engineBinaryPath)
        {
            var configPath = Path.Combine(projectPath, _configJsonFilename);
            List<string> engineArgs = new() { _projectFileCommandLineArgs, configPath, _launchEditorFlag, _launchEditorValue };

            var workingDir = Path.GetDirectoryName(engineBinaryPath) ?? _workingDirectory;
            var processStartInfo = new ProcessStartInfo(engineBinaryPath)
            {
                UseShellExecute = false,
                WorkingDirectory = workingDir
            };
            foreach (var arg in engineArgs)
                processStartInfo.ArgumentList.Add(arg);

            var engineProcess = Process.Start(processStartInfo)!;

            // WaitForInputIdle is Windows-only and only applies to GUI apps with a message pump.
            // Give the process a short grace period then check for an immediate bad exit code.
            await Task.Delay(300);
            if (engineProcess.HasExited && engineProcess.ExitCode == -2)
                throw new Exception("Failed to start the engine, invalid args");
        }
    }
}
