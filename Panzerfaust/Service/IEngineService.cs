using Panzerfaust.Models;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public interface IEngineService
    {
        Task StartAsync(string projectPath);
        Task StartAsync(string projectPath, string engineBinaryPath);
        IEnumerable<InstalledEngine> ScanInstalledEngines(string installLocation);
    }
}
