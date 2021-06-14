#pragma once
#include <Managers/IManager.h>
#include <Z_EngineDef.h>


namespace ZEngine::Managers {

	template<typename T>
	struct IAssetManager : public IManager<std::string, ZEngine::Ref<T>>
	{
		IAssetManager() =  default;
		virtual ~IAssetManager() =  default;
		
		virtual Ref<T>& Add(const char* name, const char* filename) = 0;
		virtual Ref<T>& Load(const char* filename) = 0;

		ZEngine::Ref<T>& Obtains(const char* name) {
			const auto key = std::string(name).append(this->m_suffix);
			const auto result = IManager<std::string, ZEngine::Ref<T>>::Get(key);
			assert(result.has_value() == true);

			return result->get();
		}

	protected:
		std::string m_suffix{};
	};
}