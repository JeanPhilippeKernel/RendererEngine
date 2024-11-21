using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Models
{
    internal class ErrorMessage
    {
        public string Message { get; set; } = string.Empty;
        public Exception? Exception { get; set; } = null;
    }
}
