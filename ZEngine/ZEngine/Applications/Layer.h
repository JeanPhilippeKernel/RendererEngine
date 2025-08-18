#pragma once
#include <Core/IEventable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Core/Memory/Allocator.h>
#include <GameApplication.h>
#include <ZEngineDef.h>

namespace ZEngine::Applications
{

    struct Layer : public Core::IUpdatable, public Core::IEventable, public Core::IRenderable
    {
        Layer(cstring name = "default_layer")
        {
            Name = name;
        }

        virtual ~Layer()                                                                                               = default;

        ZEngine::Core::Memory::ArenaAllocator  LocalArena                                                              = {};
        ZEngine::Core::Memory::ArenaAllocator* Arena                                                                   = nullptr;
        cstring                                Name                                                                    = nullptr;

        GameApplicationPtr                     CurrentApp                                                              = nullptr;

        virtual void                           Initialize(Core::Memory::ArenaAllocator* arena, GameApplicationPtr app) = 0;
        virtual void                           Deinitialize() {};
    };
} // namespace ZEngine::Applications
