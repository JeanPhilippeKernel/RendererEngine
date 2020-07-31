#pragma once
#include <memory>

#include "Engine.h"

using namespace Z_Engine;


#ifdef Z_ENGINE_PLATFORM_WINDOW

int main(int argc, char* argv[]) {

	std::unique_ptr<Z_Engine::Engine> engine{ CreateEngine() };
	engine->Run();

	return 0;
}
#endif