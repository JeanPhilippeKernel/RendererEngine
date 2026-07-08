#pragma once
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/IEventable.h>
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ZEngineDef.h>

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
