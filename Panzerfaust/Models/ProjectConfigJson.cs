using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace Panzerfaust.Models
{
    internal class Scene
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = "Default";
        [JsonPropertyName("isDefault")]
        public bool IsDefault { get; set; } = true;
    }

    internal class ImportDirectory
    {
        private string _defaultName = "Imported";
        private string _textureDirectory = string.Empty;
        private string _soundDirectory = string.Empty;

        [JsonPropertyName("textureDir")]
        public string TextureDirectory
        {
            get => _textureDirectory;
            set
            {
                _textureDirectory = Path.Combine(_defaultName, value);                
            }
        }
        [JsonPropertyName("soundDir")]
        public string SoundDirectory
        {
            get => _soundDirectory;
            set
            {
                _soundDirectory = Path.Combine(_defaultName, value);
            }
        }
    }

    internal class ProjectConfigJson
    {
        [JsonPropertyName("projectName")]
        public string ProjectName { get; set; } = string.Empty;
        [JsonPropertyName("version")]
        public string Version { get; set; } = "1.0.0";
        [JsonPropertyName("workingSpace")]
        public string WorkingSpace { get; set; } = ".";
        [JsonPropertyName("sceneDir")]
        public string SceneDirectory { get; set; } = "Scenes";
        [JsonPropertyName("sceneDataDir")]
        public string SceneDataDirectory { get; set; } = "SceneData";
        [JsonPropertyName("defaultImportDir")]
        public ImportDirectory DefautImportDirectory { get; set; } = new();
        [JsonPropertyName("sceneList")]
        public IList<Scene> Scenes { get; set; } = new List<Scene> { new Scene() };

        public string ToJson() => JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true });
    }
}
