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
            var key = nameof(ProjectName);
            if (_errors.ContainsKey(key))
            {
                _errors.Remove(key);
            }

            if (string.IsNullOrEmpty(name))
            {
                _errors[key] = new() { "Name can't be empty" };
            }            
            else if (string.IsNullOrWhiteSpace(name))
            {
                _errors[key] = new() { "Name can't contain only whitespaces" };
            }            
            else if (Regex.IsMatch(name, @"[\s\W]+"))
            {
                _errors[key] =  new() { "Name can't contain special characters or whitespace." };
            }

            this.RaisePropertyChanged(nameof(HasErrors));
        }

        private void ValidateProjectLocation(string? location)
        {
            var key = nameof(ProjectLocation);
            if (_errors.ContainsKey(key))
            {
                _errors.Remove(key);
            }

            if (string.IsNullOrEmpty(location))
            {
                _errors[key] = new() { "Location can't be empty" };
            }
            else if (string.IsNullOrWhiteSpace(location))
            {
                _errors[key] = new() { "Location can't contain only whitespaces" };
            }
            
            if (!Directory.Exists(location))
            {
                _errors[key] = new() { "This location isn't valid" };
            }

            this.RaisePropertyChanged(nameof(HasErrors));
        }
    }
}
