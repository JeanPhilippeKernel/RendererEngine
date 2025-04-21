#pragma once
#include <Core/IEventable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Core/Memory/Allocator.h>
#include <Windows/CoreWindow.h>

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Windows::Layers
{

    struct Layer : public Core::IUpdatable, public Core::IEventable, public Core::IRenderable
    {
        Layer(const char* name = "default_layer")
        {
            Name = name;
        }

        virtual ~Layer()                                                                      = default;

        virtual void                          Initialize(Core::Memory::ArenaAllocator* arena) = 0;
        virtual void                          Deinitialize() {};

        ZEngine::Core::Memory::ArenaAllocator LayerArena    = {};
        const char*                           Name          = nullptr;
        void*                                 ParentContext = nullptr;
        ZRawPtr(ZEngine::Windows::CoreWindow) ParentWindow  = nullptr;
    };
} // namespace ZEngine::Windows::Layers
