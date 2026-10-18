#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

namespace Tetragrama::Components
{
    /// @brief Most-recently-opened scene files, newest first, persisted as "<unix seconds>\t<path>" lines.
    struct RecentScenes
    {
        static constexpr int kMax = 8;

        struct Entry
        {
            char    Path[512] = {};
            int64_t Time      = 0;
        };

        Entry          Items[kMax] = {};
        int            Count       = 0;
        std::string    File        = {};

        static int64_t Now()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        /// @brief Load from @p file (missing file = empty list) and remember it for later saves.
        void Load(const std::string& file)
        {
            File  = file;
            Count = 0;
            for (auto& item : Items)
                item = {};
            std::ifstream in{file};
            char          line[sizeof(Entry::Path) + 32] = {}; // Path + signed 64-bit timestamp, tab, CR and terminator.
            while (Count < kMax)
            {
                if (!in.getline(line, sizeof(line)))
                {
                    if (in.eof() || in.bad())
                        break;
                    in.clear();
                    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    continue;
                }
                std::string_view record{line, static_cast<size_t>(in.gcount()) - (in.eof() ? 0u : 1u)};
                if (!record.empty() && record.back() == '\r')
                    record.remove_suffix(1);
                const size_t tab = record.find('\t');
                if (tab == std::string_view::npos || tab == 0)
                    continue;
                const auto path = record.substr(tab + 1);
                if (path.empty() || path.size() >= sizeof(Entry::Path) || path.find_first_of(std::string_view{"\0\t\r", 3}) != std::string_view::npos)
                    continue;
                Entry      entry  = {};
                const auto parsed = std::from_chars(record.data(), record.data() + tab, entry.Time);
                if (parsed.ec != std::errc{} || parsed.ptr != record.data() + tab || entry.Time < 0)
                    continue;
                ZEngine::Helpers::secure_memcpy(entry.Path, sizeof(entry.Path), path.data(), path.size());
                bool duplicate = false;
                for (int i = 0; i < Count; ++i)
                    if (ZEngine::Helpers::secure_strcmp(Items[i].Path, entry.Path) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                if (!duplicate)
                    Items[Count++] = entry;
            }
        }

        void Save() const
        {
            if (File.empty())
                return;
            std::error_code ec = {};
            std::filesystem::create_directories(std::filesystem::path(File).parent_path(), ec);
            std::ofstream out(File, std::ios::trunc);
            for (int i = 0; i < Count; ++i)
                out << Items[i].Time << '\t' << Items[i].Path << '\n';
        }

        /// @brief Move @p path to the front (adding it if new) and persist.
        void Push(const char* path)
        {
            if (!path || !*path)
                return;
            size_t length = 0;
            while (length < sizeof(Entry::Path) && path[length])
            {
                if (path[length] == '\t' || path[length] == '\r' || path[length] == '\n')
                    return; // These characters cannot roundtrip through the history format.
                ++length;
            }
            if (length >= sizeof(Entry::Path))
                return;
            // Copy before shifting so callers can safely pass an existing Items[i].Path.
            Entry entry = {};
            ZEngine::Helpers::secure_memcpy(entry.Path, sizeof(entry.Path), path, length);
            entry.Time = Now();
            int at     = -1;
            for (int i = 0; i < Count; ++i)
                if (ZEngine::Helpers::secure_strcmp(Items[i].Path, entry.Path) == 0)
                {
                    at = i;
                    break;
                }
            if (at < 0)
                at = (Count < kMax) ? Count++ : kMax - 1;
            for (int i = at; i > 0; --i)
                Items[i] = Items[i - 1];
            Items[0] = entry;
            Save();
        }

        void Clear()
        {
            Count = 0;
            for (auto& item : Items)
                item = {};
            Save();
        }

        /// @brief "2 min ago", "Yesterday", "Mon", "Last week" style label for the right of a menu item.
        static void Age(int64_t then, char* out, size_t cap)
        {
            if (!out || !cap)
                return;
            const int64_t  now = Now();
            // Unsigned subtraction also handles INT64_MIN without signed overflow.
            const uint64_t d   = then < now ? static_cast<uint64_t>(now) - static_cast<uint64_t>(then) : 0u;
            if (d < 60)
                std::snprintf(out, cap, "Just now");
            else if (d < 3600)
                std::snprintf(out, cap, "%lld min ago", (long long) (d / 60));
            else if (d < 86400)
                std::snprintf(out, cap, "%lld h ago", (long long) (d / 3600));
            else if (d < 2 * 86400)
                std::snprintf(out, cap, "Yesterday");
            else if (d < 7 * 86400)
                std::snprintf(out, cap, "%lld days ago", (long long) (d / 86400));
            else
                std::snprintf(out, cap, "Last week");
        }

        /// @brief File stem of @p path for display ("…/L_Blockout_Arena.zescene" -> "L_Blockout_Arena").
        static void Stem(const char* path, char* out, size_t cap)
        {
            if (!out || !cap)
                return;
            if (!path)
            {
                out[0] = '\0';
                return;
            }
            // Menu hot path: one pass, no filesystem/path conversions or heap allocations.
            // Accept either platform's separators for history copied between machines.
            const char* stem = path;
            const char* dot  = nullptr;
            const char* end  = path;
            for (; *end; ++end)
                if (*end == '/' || *end == '\\')
                {
                    stem = end + 1;
                    dot  = nullptr;
                }
                else if (*end == '.' && end != stem)
                    dot = end;
            if (end - stem == 2 && stem[0] == '.' && stem[1] == '.')
                dot = nullptr;
            const size_t length = std::min(cap - 1, static_cast<size_t>((dot ? dot : end) - stem));
            ZEngine::Helpers::secure_memmove(out, cap, stem, length);
            out[length] = '\0';
        }
    };
    /// @brief Process-wide recent-scene list (the editor has a single shell).
    inline RecentScenes& Recent()
    {
        static RecentScenes list = {};
        return list;
    }
} // namespace Tetragrama::Components
