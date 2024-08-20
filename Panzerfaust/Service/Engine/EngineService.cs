using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service.Engine
{
    public class EngineService : IEngineService
    {
        string enginePath = string.Empty;
        string configuration = string.Empty;
        string workingDirectory = string.Empty;
        string exampleProjectConfig = string.Empty;
        ProcessStartInfo processStartInfo = null;

        const string editorAppName = "zEngineEditor";
        List<string> engineArgs = new List<string> { "--projectConfigFile" };

        private List<string> _engines = new List<string>();
        
        public EngineService() 
        {
            LoadConfig();
        }

        private void LoadConfig()
        {
#if DEBUG
            configuration = "Debug";
            exampleProjectConfig = @$"{Environment.CurrentDirectory}/Examples/projectConfig.json";
            engineArgs.Add(exampleProjectConfig);
#else
            configuration = "Release";
#endif

            string engineExtension = string.Empty;

#if _WIN32
            engineExtension = ".exe";
#endif
            enginePath = @$"{Environment.CurrentDirectory}/Editor/{editorAppName}{engineExtension}";
            workingDirectory = @$"{Environment.CurrentDirectory}/Editor";

            processStartInfo = new ProcessStartInfo(enginePath, string.Join(" ", engineArgs))
            {
                UseShellExecute = false,
                WorkingDirectory = workingDirectory
            };
        }

        public async Task Start()
        {
            var engineProcess = Process.Start(processStartInfo);
             await engineProcess.WaitForExitAsync();

            if (engineProcess.ExitCode == -2)
            {
                throw new Exception("Failed to start the engine, invalid  args");
            }
        }

        public Task Stop()
        {
            throw new NotImplementedException();
        }
    }
}
