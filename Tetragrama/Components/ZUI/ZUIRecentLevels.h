#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace Tetragrama::Components
{
    /// @brief Most-recently-opened level files, newest first, persisted as "<unix seconds>\t<path>" lines.
    struct RecentLevels
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
            std::ifstream in(file);
            std::string   line = {};
            while (Count < kMax && std::getline(in, line))
            {
                const size_t tab = line.find('\t');
                if (tab == std::string::npos || line.size() - tab - 1 >= sizeof(Entry::Path))
                    continue;
                Items[Count].Time = std::strtoll(line.c_str(), nullptr, 10);
                std::snprintf(Items[Count].Path, sizeof(Entry::Path), "%s", line.c_str() + tab + 1);
                ++Count;
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
            int at = -1;
            for (int i = 0; i < Count; ++i)
                if (ZEngine::Helpers::secure_strcmp(Items[i].Path, path) == 0)
                    at = i;
            if (at < 0)
                at = (Count < kMax) ? Count++ : kMax - 1;
            for (int i = at; i > 0; --i)
                Items[i] = Items[i - 1];
            std::snprintf(Items[0].Path, sizeof(Entry::Path), "%s", path);
            Items[0].Time = Now();
            Save();
        }

        void Clear()
        {
            Count = 0;
            Save();
        }

        /// @brief "2 min ago", "Yesterday", "Mon", "Last week" style label for the right of a menu item.
        static void Age(int64_t then, char* out, size_t cap)
        {
            const int64_t d = Now() - then;
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
            const std::string s = std::filesystem::path(path).stem().string();
            std::snprintf(out, cap, "%s", s.c_str());
        }
    };
    /// @brief Process-wide recent-level list (the editor has a single shell).
    inline RecentLevels& Recent()
    {
        static RecentLevels list = {};
        return list;
    }
} // namespace Tetragrama::Components
