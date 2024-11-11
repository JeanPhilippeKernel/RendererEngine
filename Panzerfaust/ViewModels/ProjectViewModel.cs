using Panzerfaust.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class ProjectViewModel
    {
        private readonly Project _project;

        public ProjectViewModel(Project p) => _project = p;

        public string Name => _project.Name;
        public string Path => _project.Fullpath;
        public string UpdatedDate => _project.UpdateDate.ToShortDateString();
    }
}
