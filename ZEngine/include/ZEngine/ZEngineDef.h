#pragma once
#include <memory>
#include <stdlib.h>


#define BIT(x) (1 << (x))
#define Z_ENGINE_EXIT_FAILURE()	exit(EXIT_FAILURE)

namespace ZEngine {

	template<typename T>
	using Ref =  std::shared_ptr<T>;

	template<typename T>
	using WeakRef = std::weak_ptr<T>;

	template<typename T>
	using Scope = std::unique_ptr<T>;
}