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
        public uint Id { get; set; }
        public string Name { get; set; } = string.Empty;
        public string Fullpath { get; set; } = string.Empty;
        public DateTime CreationDate { get; set; }
        public DateTime UpdateDate { get; set; }
    }
}
