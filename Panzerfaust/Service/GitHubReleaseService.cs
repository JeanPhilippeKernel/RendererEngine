using Panzerfaust.Models;
using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public class GitHubReleaseService : IGitHubReleaseService
    {
        private const string ApiUrl = "https://api.github.com/repos/JeanPhilippeKernel/RendererEngine/releases";
        private readonly HttpClient _http;

        public GitHubReleaseService()
        {
            _http = new HttpClient();
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("Panzerfaust/1.0");
        }

        public async Task<IEnumerable<GitHubRelease>> GetReleasesAsync()
        {
            var json = await _http.GetStringAsync(ApiUrl);
            return JsonSerializer.Deserialize<List<GitHubRelease>>(json) ?? new();
        }
    }
}
