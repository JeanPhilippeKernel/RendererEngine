using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Models
{
    public class Project
    {
        [JsonProperty("projectName")]
        public string ProjectName { get; set; }

        [JsonProperty("version")]
        public string Version { get; set; }

        [JsonProperty("workingSpace")]
        public string WorkingSpace { get; set; }

        [JsonProperty("defaultImportDir")]
        public DefaultImportDir DefaultImportDir { get; set; }

        [JsonProperty("sceneDir")]
        public string SceneDir { get; set; }

        [JsonProperty("sceneDataDir")]
        public string SceneDataDir { get; set; }

        [JsonProperty("sceneList")]
        public IList<SceneList> SceneList { get; set; }
    }
}
