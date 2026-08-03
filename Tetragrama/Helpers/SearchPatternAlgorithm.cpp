#include <Tetragrama/Helpers/SearchPatternAlgorithm.h>
#include <ZEngine/Helpers/MemoryOperations.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Helpers;

namespace Tetragrama::Helpers
{
    void KMPbuildTable(const char* pattern, ArrayView<int> lps)
    {
        int m  = secure_strlen(pattern);
        int j  = 0; // length of previous longest prefix suffix

        lps[0] = 0; // lps[0] is always 0
        for (int i = 1; i < m; ++i)
        {
            while (j > 0 && std::tolower(pattern[i]) != std::tolower(pattern[j]))
            {
                j = lps[j - 1];
            }
            if (std::tolower(pattern[i]) == std::tolower(pattern[j]))
            {
                ++j;
            }
            lps[i] = j;
        }
    }

    bool KMPSearch(ArenaAllocator* arena, const char* text, const char* pattern)
    {
        int n = secure_strlen(text);
        int m = secure_strlen(pattern);

        // Edge case: empty pattern
        if (m == 0)
            return true;

        Array<int> lps{};
        lps.init(arena, m, m); // longest prefix suffix table
        KMPbuildTable(pattern, lps);

        int i = 0; // index for text[]
        int j = 0; // index for pattern[]

        while (i < n)
        {
            if (std::tolower(text[i]) == std::tolower(pattern[j]))
            {
                ++i;
                ++j;
            }

            if (j == m)
            { // found a match
                return true;
            }
            else if (i < n && std::tolower(text[i]) != std::tolower(pattern[j]))
            {
                if (j != 0)
                {
                    j = lps[j - 1];
                }
                else
                {
                    ++i;
                }
            }
        }
        return false; // no match found
    }
} // namespace Tetragrama::Helpers
