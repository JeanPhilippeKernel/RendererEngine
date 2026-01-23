using Panzerfaust.Models;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
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
        const string _editorCommandLineArgs = "--launchEditor 1";

        public EngineService()
        {
            string engineExtension = string.Empty;
#if _WIN32
            engineExtension = ".exe";
#endif
            _enginePath = Path.Combine(Environment.CurrentDirectory, $"{_launcherCLIAppName}{engineExtension}");
            _workingDirectory = Environment.CurrentDirectory;
        }

        public async Task StartAsync(string path)
        {
            List<string> engineArgs = new() { _editorCommandLineArgs, _projectFileCommandLineArgs, Path.Combine(path, _configJsonFilename) };

            var processStartInfo = new ProcessStartInfo(_enginePath, string.Join(" ", engineArgs))
            {
                UseShellExecute = false,
                WorkingDirectory = _workingDirectory
            };

            var engineProcess = Process.Start(processStartInfo);
            bool processIdle = engineProcess.WaitForInputIdle();

            if (processIdle) { return; }
            else
            {
                await engineProcess.WaitForExitAsync();
                if (engineProcess.ExitCode == -2)
                {
                    throw new Exception("Failed to start the engine, invalid args");
                }
            }
        }
    }
}
