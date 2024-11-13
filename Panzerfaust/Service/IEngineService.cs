using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public interface IEngineService
    {
        Task StartAsync(string path);
    }
}
