#pragma once
#include <CoreWindow.h>
#include <KeyCode.h>
#include <ZEngineDef.h>
#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace ZEngine::Windows::Inputs
{

    struct IDevice : public Helpers::RefCounted
    {
    public:
        virtual ~IDevice() = default;

        template <typename T, typename = std::enable_if_t<std::is_base_of_v<IDevice, T>>>
        static const T* As() noexcept
        {

            const std::type_info& type = typeid(T);
            auto                  it   = m_devices.find(std::string(type.name()));

            if (it != std::end(m_devices))
            {
                return reinterpret_cast<T*>(it->second.get());
            }

            Ref<IDevice> device_ptr = CreateRef<T>();

            auto pair = m_devices.emplace(std::make_pair(std::string(type.name()), std::move(device_ptr)));
            return reinterpret_cast<T*>(pair.first->second.get());
        }

        virtual bool IsKeyPressed(ZENGINE_KEYCODE key, const Ref<Windows::CoreWindow>& window) const  = 0;
        virtual bool IsKeyReleased(ZENGINE_KEYCODE key, const Ref<Windows::CoreWindow>& window) const = 0;

        virtual std::string_view GetName() const
        {
            return m_name;
        }

    protected:
        IDevice(std::string_view name = "abstract_device") : m_name(name) {}
        static std::unordered_map<std::string, Ref<IDevice>> m_devices;
        std::string                                          m_name;
    };
} // namespace ZEngine::Windows::Inputs
