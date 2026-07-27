using Panzerfaust.Models;
using System.Collections.Generic;
using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public class PolyHavenService : IPolyHavenService
    {
        private const string BaseUrl = "https://api.polyhaven.com";
        private readonly HttpClient _http;

        public PolyHavenService()
        {
            _http = new HttpClient();
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("Panzerfaust/1.0");
        }

        public async Task<IEnumerable<PolyHavenAsset>> GetModelsAsync()
        {
            var json = await _http.GetStringAsync($"{BaseUrl}/assets?type=models");
            var doc = JsonSerializer.Deserialize<JsonObject>(json) ?? new JsonObject();
            var results = new List<PolyHavenAsset>();
            foreach (var kv in doc)
            {
                var asset = kv.Value.Deserialize<PolyHavenAsset>() ?? new PolyHavenAsset();
                asset.Id = kv.Key;
                results.Add(asset);
            }
            return results;
        }

        public async Task<JsonObject> GetFilesAsync(string assetId)
        {
            var json = await _http.GetStringAsync($"{BaseUrl}/files/{assetId}");
            return JsonSerializer.Deserialize<JsonObject>(json) ?? new JsonObject();
        }
    }
}
