#pragma once
#include <ZEngineDef.h>
#include <unordered_map>
#include <functional>
#include <string_view>
#include <vector>
#include <tuple>
#include <type_traits>
#include <mutex>
#include <any>
#include <Components/UIComponent.h>
#include <Message.h>

namespace Tetragrama::Messengers
{

    template <typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
    using action_callback = std::function<void(TMessage&)>;

    class Messenger
    {
        using Component                 = std::any;
        using ComponentAction           = std::tuple<Component, std::any>;
        using ComponentActionCollection = std::vector<ComponentAction>;

    public:
        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void Send(std::string_view token, TMessage&& message)
        {

            try
            {
                {
                    auto find_it = m_routing_map.find(std::string(token));
                    if (find_it == std::end(m_routing_map))
                    {
                        return;
                    }

                    for (const auto& recipient : find_it->second)
                    {
                        const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(std::get<0>(recipient));
                        if (!component.expired())
                        {
                            const auto& callback = std::any_cast<action_callback<TMessage>>(std::get<1>(recipient));
                            callback(message);
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void Send(std::string_view token, TMessage&& message, std::function<void(void)>&& send_completion_callback)
        {
            try
            {
                {
                    auto find_it = m_routing_map.find(std::string(token));
                    if (find_it == std::end(m_routing_map))
                    {
                        return;
                    }

                    for (const auto& recipient : find_it->second)
                    {
                        const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(std::get<0>(recipient));
                        if (!component.expired())
                        {
                            const auto& callback = std::any_cast<action_callback<TMessage>>(std::get<1>(recipient));
                            callback(message);
                        }
                    }
                    if (send_completion_callback)
                    {
                        send_completion_callback();
                    }
                }
            }
            catch (...)
            {
            }
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void SendAsync(std::string_view token, TMessage&& message)
        {

            std::thread(
                [this, token, message = std::move(message)]() mutable
                {
                    try
                    {
                        std::unique_lock lock(m_mutex);
                        {
                            auto find_it = m_routing_map.find(std::string(token));
                            if (find_it == std::end(m_routing_map))
                            {
                                return;
                            }

                            for (const auto& recipient : find_it->second)
                            {
                                const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(std::get<0>(recipient));
                                if (!component.expired())
                                {
                                    const auto& callback = std::any_cast<action_callback<TMessage>>(std::get<1>(recipient));
                                    callback(message);
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                })
                .detach();
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void SendAsync(std::string_view token, TMessage&& message, std::function<void(void)>&& send_completion_callback)
        {

            std::thread(
                [this, token, message = std::move(message), send_completion_callback = std::move(send_completion_callback)]() mutable
                {
                    try
                    {
                        std::unique_lock lock(m_mutex);
                        {
                            auto find_it = m_routing_map.find(std::string(token));
                            if (find_it == std::end(m_routing_map))
                            {
                                return;
                            }

                            for (const auto& recipient : find_it->second)
                            {
                                const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(std::get<0>(recipient));
                                if (!component.expired())
                                {
                                    const auto& callback = std::any_cast<action_callback<TMessage>>(std::get<1>(recipient));
                                    callback(message);
                                }
                            }
                            if (send_completion_callback)
                            {
                                send_completion_callback();
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                })
                .detach();
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void Register(ZEngine::Ref<TRecipient> recipient, std::string_view token, action_callback<TMessage>&& callback)
        {
            std::string routing_token = token.data();

            auto find_it = m_routing_map.find(routing_token);

            ZEngine::WeakRef<TRecipient> weak_recipient(recipient);
            ComponentAction              component_action = std::make_tuple(std::move(weak_recipient), std::move(callback));
            if (find_it == std::end(m_routing_map))
            {
                m_routing_map[routing_token] = ComponentActionCollection{component_action};
                return;
            }

            m_routing_map[routing_token].push_back(std::move(component_action));
        }

    private:
        std::mutex                                                 m_mutex;
        std::unordered_map<std::string, ComponentActionCollection> m_routing_map;
    };

    struct IMessenger
    {
        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static void Send(std::string_view token, TMessage&& message)
        {
            m_messenger->Send<TRecipient, TMessage>(token, std::forward<TMessage>(message));
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static void Send(std::string_view token, TMessage&& message, std::function<void(void)>&& completion_callback)
        {
            m_messenger->Send<TRecipient, TMessage>(token, std::forward<TMessage>(message), std::move(completion_callback));
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static void SendAsync(std::string_view token, TMessage&& message, std::function<void(void)>&& completion_callback)
        {
            m_messenger->SendAsync<TRecipient, TMessage>(token, std::forward<TMessage>(message), std::move(completion_callback));
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static void SendAsync(std::string_view token, TMessage&& message)
        {
            m_messenger->SendAsync<TRecipient, TMessage>(token, std::forward<TMessage>(message));
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static void Register(ZEngine::Ref<TRecipient> recipient, std::string_view token, action_callback<TMessage>&& callback)
        {
            m_messenger->Register<TRecipient, TMessage>(recipient, token, std::move(callback));
        }

    private:
        inline static ZEngine::Ref<Messenger> m_messenger = std::make_shared<Messenger>();
    };

} // namespace Tetragrama::Messengers
