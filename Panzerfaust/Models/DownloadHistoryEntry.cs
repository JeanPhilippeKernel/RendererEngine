namespace Panzerfaust.Models
{
    public class DownloadHistoryEntry
    {
        public string Category { get; set; } = string.Empty; // "engine" | "asset"
        public string Version { get; set; } = string.Empty;
        public string Status { get; set; } = string.Empty;
        public int Progress { get; set; }
        public string ErrorMessage { get; set; } = string.Empty;
    }
}
