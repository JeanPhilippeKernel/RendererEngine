#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/Windows/Inputs/KeyCode.h>
#include <map>
#include <type_traits>

namespace ZEngine::Windows::Inputs
{

    struct IDevice
    {
        IDevice(const char* name = "abstract_device") : m_name(name) {}
        virtual ~IDevice() = default;
        const char*                                    m_name;
        static Core::Memory::ArenaAllocator*           Arena;
        static std::map<const char*, ZRawPtr(IDevice)> Devices;

        static void                                    Initialize(Core::Memory::ArenaAllocator* arena)
        {
            Arena = arena;
        }

        template <typename T, typename = std::enable_if_t<std::is_base_of_v<IDevice, T>>>
        static const T* As() noexcept
        {
            const std::type_info& type = typeid(T);
            auto                  it   = Devices.find(type.name());

            if (it != std::end(Devices))
            {
                return reinterpret_cast<T*>(it->second);
            }

            IDevice* device = ZPushStructCtor(Arena, T);
            auto     pair   = Devices.emplace(std::make_pair(type.name(), device));
            return reinterpret_cast<T*>(pair.first->second);
        }

        virtual bool        IsKeyPressed(ZENGINE_KEYCODE key, Windows::CoreWindow* const window) const  = 0;

        virtual bool        IsKeyReleased(ZENGINE_KEYCODE key, Windows::CoreWindow* const window) const = 0;

        virtual const char* GetName() const
        {
            return m_name;
        }
    };
} // namespace ZEngine::Windows::Inputs
