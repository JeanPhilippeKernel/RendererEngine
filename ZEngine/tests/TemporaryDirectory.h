#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace ZEngine::Tests
{
    // Own only a directory created by this fixture. Never delete an existing
    // shared test path, and retain the native path on Unicode Windows hosts.
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()                                     = default;
        TemporaryDirectory(const TemporaryDirectory&)            = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        ~TemporaryDirectory()
        {
            if (m_created)
            {
                std::error_code error = {};
                std::filesystem::remove_all(m_path, error);
            }
        }

        bool Create(const std::filesystem::path& prefix, std::error_code& error)
        {
            if (m_created || prefix.empty() || prefix.has_parent_path())
            {
                error = std::make_error_code(std::errc::invalid_argument);
                return false;
            }
            const auto parent = std::filesystem::temp_directory_path(error);
            if (error)
                return false;
            static std::atomic<uint64_t> sequence = 0;
            const auto                   stamp    = std::chrono::steady_clock::now().time_since_epoch().count();
            for (uint32_t attempt = 0; attempt < 64; ++attempt)
            {
                m_path     = parent / prefix;
                m_path    += "_" + std::to_string(stamp) + "_" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
                // The exclusive native mkdir, not the timestamp/sequence, grants
                // ownership even if another process chooses the same name.
                m_created  = std::filesystem::create_directory(m_path, error);
                if (m_created)
                    return true;
                if (error && error != std::errc::file_exists)
                    return false;
            }
            error = std::make_error_code(std::errc::file_exists);
            return false;
        }

        const std::filesystem::path& Path() const
        {
            return m_path;
        }

    private:
        std::filesystem::path m_path    = {};
        bool                  m_created = false;
    };
} // namespace ZEngine::Tests
