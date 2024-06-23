using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service.Engine
{
    public interface IEngineService
    {
        Task Start();
        Task Stop();
    }
}
