using ReactiveUI;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.RegularExpressions;

namespace Panzerfaust.ViewModels
{
    internal class EnginePickerViewModel : ReactiveObject
    {
        private static readonly Regex RcPattern = new(@"-rc\.", RegexOptions.IgnoreCase);

        private readonly ObservableCollection<InstalledEngineViewModel> _allEngines;

        public string ProjectName { get; }
        public ObservableCollection<InstalledEngineViewModel> FilteredEngines { get; } = new();
        public bool HasEngines => _allEngines.Count > 0;

        private string _searchText = string.Empty;
        public string SearchText
        {
            get => _searchText;
            set
            {
                this.RaiseAndSetIfChanged(ref _searchText, value);
                ApplyFilter();
            }
        }

        private InstalledEngineViewModel? _selectedEngine;
        public InstalledEngineViewModel? SelectedEngine
        {
            get => _selectedEngine;
            set
            {
                this.RaiseAndSetIfChanged(ref _selectedEngine, value);
                this.RaisePropertyChanged(nameof(IsRcSelected));
                this.RaisePropertyChanged(nameof(CanOpen));
            }
        }

        public bool IsRcSelected => _selectedEngine != null && RcPattern.IsMatch(_selectedEngine.Version);
        public bool CanOpen => _selectedEngine != null;

        public EnginePickerViewModel(string projectName, ObservableCollection<InstalledEngineViewModel> engines)
        {
            ProjectName = projectName;
            _allEngines = engines;
            ApplyFilter();
        }

        private void ApplyFilter()
        {
            var term = _searchText.Trim();
            FilteredEngines.Clear();
            foreach (var e in _allEngines)
                if (string.IsNullOrEmpty(term) || e.Version.Contains(term, System.StringComparison.OrdinalIgnoreCase))
                    FilteredEngines.Add(e);

            if (SelectedEngine == null || !FilteredEngines.Contains(SelectedEngine))
                SelectedEngine = FilteredEngines.FirstOrDefault();
        }
    }
}
