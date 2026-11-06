#pragma once
#include <cstdint>
#include <filesystem>

namespace ZEngine::Cook::Tests
{
    // Test-only offline-project smoke, not a shipping CLI. The caller excludes
    // editor/import writers and supplies an empty directory outside the project.
    int RunProjectSmoke(const std::filesystem::path& project, const std::filesystem::path& workspace, uint64_t maximum_working_bytes, uint32_t expected_entities = UINT32_MAX);
} // namespace ZEngine::Cook::Tests
