#pragma once
#include <CoreWindow.h>
#include <KeyCode.h>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <type_traits>

namespace ZEngine::Windows::Inputs
{

    struct IDevice
    {
        virtual ~IDevice() = default;

        template <typename T, typename = std::enable_if_t<std::is_base_of_v<IDevice, T>>>
        static const T* As() noexcept
        {
            const std::type_info& type = typeid(T);
            auto                  it   = m_devices.find(std::string(type.name()));

            if (it != std::end(m_devices))
            {
                return reinterpret_cast<T*>(&it->second);
            }

            IDevice device = T();
            auto    pair   = m_devices.emplace(std::make_pair(std::string(type.name()), device));
            return reinterpret_cast<T*>(&(pair.first->second));
        }

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, Windows::CoreWindow* const window) const
        {
            return false;
        }

        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, Windows::CoreWindow* const window) const
        {
            return false;
        }

        virtual std::string_view GetName() const
        {
            return m_name;
        }

    protected:
        IDevice(std::string_view name = "abstract_device") : m_name(name) {}
        static std::map<std::string, IDevice> m_devices;
        std::string                           m_name;
    };
} // namespace ZEngine::Windows::Inputs
