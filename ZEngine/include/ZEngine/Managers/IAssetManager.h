#pragma once
#include <Managers/IManager.h>
#include <ZEngineDef.h>

namespace ZEngine::Managers {

    template <typename T>
    struct IAssetManager : public IManager<std::string, ZEngine::Ref<T>> {
        IAssetManager()          = default;
        virtual ~IAssetManager() = default;

        /**
         * Add an asset to the Asset manager store
         *
         * @param name Name of the asset. This name must be unique in the entire store
         * @param filename Path to find the asset file in the system
         * @return An asset instance
         */
        virtual Ref<T>& Add(const char* name, const char* filename) = 0;

        /**
         * Add a asset to the Asset manager store
         *
         * @param filename Path to find the asset file in the system
         * @return An asset instance
         */
        virtual Ref<T>& Load(const char* filename) = 0;

        /**
         * Get an asset instance from the Asset manager store
         *
         * @param name Name of the asset.
         * @return An asset instance.
         *		  The asset must exists in the store, otherwise an assertion will be raised
         */
        ZEngine::Ref<T>& Obtains(const char* name) {
            const auto key    = std::string(name).append(this->m_suffix);
            const auto result = IManager<std::string, ZEngine::Ref<T>>::Get(key);
            assert(result.has_value() == true);

            return result->get();
        }

    protected:
        std::string m_suffix{};
    };
} // namespace ZEngine::Managers
