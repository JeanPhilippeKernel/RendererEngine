#pragma once
#include <memory>
#include "Engine.h"

using namespace ZEngine;


#ifdef Z_ENGINE_PLATFORM

int main(int argc, char* argv[]) {

	std::unique_ptr<ZEngine::Engine> engine{ CreateEngine() };
	engine->Initialize();
	engine->Run();

	return 0;
}
#endif