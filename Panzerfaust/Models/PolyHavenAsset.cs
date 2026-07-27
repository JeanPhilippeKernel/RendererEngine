using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace Panzerfaust.Models
{
    public class PolyHavenAsset
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = string.Empty;

        [JsonPropertyName("download_count")]
        public int DownloadCount { get; set; }

        [JsonPropertyName("poly_count")]
        public int PolyCount { get; set; }

        [JsonPropertyName("categories")]
        public List<string> Categories { get; set; } = new();

        [JsonPropertyName("tags")]
        public List<string> Tags { get; set; } = new();

        // Populated by the service from the dict key
        public string Id { get; set; } = string.Empty;

        public string ThumbnailUrl =>
            $"https://cdn.polyhaven.com/asset_img/thumbs/{Id}.png?width=256&height=256";
    }

    public class PolyHavenFileFormat
    {
        [JsonPropertyName("url")]
        public string Url { get; set; } = string.Empty;

        [JsonPropertyName("size")]
        public long Size { get; set; }
    }
}
