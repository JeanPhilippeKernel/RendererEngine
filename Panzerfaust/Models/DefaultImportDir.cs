using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Models
{
    public class DefaultImportDir
    {

        [JsonProperty("textureDir")]
        public string TextureDir { get; set; }

        [JsonProperty("soundDir")]
        public string SoundDir { get; set; }
    }
}
