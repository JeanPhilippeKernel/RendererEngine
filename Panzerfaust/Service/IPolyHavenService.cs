using Panzerfaust.Models;
using System.Collections.Generic;
using System.Text.Json.Nodes;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public interface IPolyHavenService
    {
        Task<IEnumerable<PolyHavenAsset>> GetModelsAsync();
        Task<JsonObject> GetFilesAsync(string assetId);
    }
}
