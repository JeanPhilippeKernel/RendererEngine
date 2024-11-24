using ReactiveUI;
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace Panzerfaust.ViewModels
{
    internal partial class ProjectWindowViewModel : INotifyDataErrorInfo
    {
        private Dictionary<string, List<string>> _errors = new();

        public bool HasErrors => _errors.Count > 0;

        public event EventHandler<DataErrorsChangedEventArgs>? ErrorsChanged;

        public IEnumerable GetErrors(string? propertyName)
        {
            if ((propertyName != null) && _errors.ContainsKey(propertyName))
            {
                return _errors[propertyName];
            }
            return new List<string>();
        }

        private void ValidateProjectName(string? name)
        {
            if (_errors.ContainsKey(nameof(ProjectName)))
            {
                _errors[nameof(ProjectName)].Clear();
            }

            if (string.IsNullOrEmpty(name))
            {
                _errors[nameof(ProjectName)] = new() { "Name can't be empty" };
            }            
            else if (string.IsNullOrWhiteSpace(name))
            {
                _errors[nameof(ProjectName)] = new() { "Name can't contain only whitespaces" };
            }            
            else if (Regex.IsMatch(name, @"[\s\W]+"))
            {
                _errors[nameof(ProjectName)] =  new() { "Name can't contain special characters or whitespace." };
            }

            this.RaisePropertyChanged(nameof(HasErrors));
        }

        private void ValidateProjectLocation(string? location)
        {
            if (_errors.ContainsKey(nameof(ProjectLocation)))
            {
                _errors[nameof(ProjectLocation)].Clear();
            }

            if (string.IsNullOrEmpty(location))
            {
                _errors[nameof(ProjectLocation)] = new() { "Location can't be empty" };
            }
            else if (string.IsNullOrWhiteSpace(location))
            {
                _errors[nameof(ProjectLocation)] = new() { "Location can't contain only whitespaces" };
            }
            
            if (!Directory.Exists(location))
            {
                _errors[nameof(ProjectLocation)] = new() { "This location isn't valid" };
            }

            this.RaisePropertyChanged(nameof(HasErrors));
        }
    }
}
