using Panzerfaust.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    internal interface IProjectService
    {
        Task<IEnumerable<Project>> LoadProjectsAsync();
        Task SaveAsync(Project p);
        Task<Project?> CreateAsync(string name, string path, DateTime creationTime, DateTime updateTime);
    }
}
