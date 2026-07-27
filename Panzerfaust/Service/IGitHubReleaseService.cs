using Panzerfaust.Models;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Panzerfaust.Service
{
    public interface IGitHubReleaseService
    {
        Task<IEnumerable<GitHubRelease>> GetReleasesAsync();
    }
}
