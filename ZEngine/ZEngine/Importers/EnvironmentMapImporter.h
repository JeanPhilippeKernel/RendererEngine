#pragma once
#include <IAssetImporter.h>

namespace ZEngine::Importers
{
    class EnvironmentMapImporter : public IAssetImporter
    {
    public:
        EnvironmentMapImporter()          = default;
        virtual ~EnvironmentMapImporter() = default;

        virtual std::future<void> ImportAsync(const char* filename, const ImportConfiguration& config) override;
    };
} // namespace ZEngine::Importers
