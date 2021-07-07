#pragma once
#include <memory>


#define BIT(x) (1 << (x))

namespace ZEngine {

	template<typename T>
	using Ref =  std::shared_ptr<T>;

	template<typename T>
	using WeakRef = std::weak_ptr<T>;

	template<typename T>
	using Scope = std::unique_ptr<T>;
}