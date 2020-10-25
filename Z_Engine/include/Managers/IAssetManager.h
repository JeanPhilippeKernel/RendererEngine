#pragma once
#include "IManager.h"
#include "../Z_EngineDef.h"


namespace Z_Engine::Managers {

	template<typename T>
	struct IAssetManager : public IManager<std::string, Z_Engine::Ref<T>>
	{
		IAssetManager() =  default;
		virtual ~IAssetManager() =  default;
		
		virtual Ref<T>& Add(const char* name, const char* filename) = 0;
		virtual Ref<T>& Load(const char* filename) = 0;

		Z_Engine::Ref<T>& Obtains(const char* name) {
			const auto key = std::string(name).append(this->m_suffix);
			const auto result = IManager<std::string, Z_Engine::Ref<T>>::Get(key);
			assert(result.has_value() == true);

			return result->get();
		}

	protected:
		std::string m_suffix{};
	};
}