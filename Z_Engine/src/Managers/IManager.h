#pragma once
#include <string>
#include <type_traits>
#include <unordered_map>
#include <optional>

namespace Z_Engine::Managers {
	
	template<
		typename T, typename K, 
		typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_same_v<T, std::string>>
	>
	struct IManager {
		IManager()					= delete;
		IManager(const IManager&)	= delete;
		~IManager()					= delete;

	protected:
		static constexpr auto Exists(const T& key) -> std::pair<bool, typename std::unordered_map<T, K>::iterator> {
			bool result{false};
			typename std::unordered_map<T, K>::iterator it = std::find_if(std::begin(m_collection), std::end(m_collection),
				[&](const std::pair<T, K>& item) {
					if (std::is_arithmetic_v<T>) {
						result = item.first == key;
					}
					
					else if (std::is_same_v<T, std::string>) {
						result = item.first.compare(key) == 0;
					}

					return result;
				}
			);

			return std::make_pair(result, it);
		}

		static constexpr void Add(const T& key, const K& val) {
			const auto& kv =  Exists(key);

			if(kv.first) return;

			m_collection.emplace(std::make_pair(key, val));

		 }
	
		static constexpr std::optional<std::reference_wrapper<K>> Get(const T& key) {

			const auto& kv = Exists(key);
			if(kv.first) {
				return kv.second->second;
			}
			return std::nullopt;
		}

		static constexpr std::unordered_map<T, K>& GetAll() { return m_collection; }

	protected:
		inline static std::unordered_map<T, K> m_collection{};
	};

}

