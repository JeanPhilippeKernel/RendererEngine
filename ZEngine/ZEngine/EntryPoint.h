#pragma once
#include <memory>
#include "Engine.h"

#ifdef ZENGINE_PLATFORM

int main(int argc, char* argv[])
{

    std::unique_ptr<ZEngine::Engine> engine{ZEngine::CreateEngine()};
    engine->Initialize();
    engine->Start();

    return 0;
}
#endif
