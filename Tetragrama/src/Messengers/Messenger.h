#pragma once
#include <ZEngineDef.h>
#include <unordered_map>
#include <functional>
#include <string_view>
#include <vector>
#include <mutex>
#include <Components/UIComponent.h>
#include <Message.h>
#include <ZEngine/Core/Coroutine.h>

namespace Tetragrama::Messengers
{
    using action_callback = std::function<std::future<void>(void* const message)>;

    class Messenger
    {
        using Component                 = void*;
        using ComponentActionPair       = std::pair<Component, action_callback>;
        using ComponentActionCollection = std::vector<ComponentActionPair>;

    public:
        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void Send(std::string_view token, TMessage&& message)
        {

            // try
            //{
            //     {
            //         auto find_it = m_routing_map.find(std::string(token));
            //         if (find_it == std::end(m_routing_map))
            //         {
            //             return;
            //         }

            //        for (const ComponentActionPair& recipient : find_it->second)
            //        {
            //            const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(recipient.first);
            //            if (!component.expired())
            //            {
            //                const auto& callback = std::any_cast<action_callback<TMessage>>(recipient.second);
            //                callback(message);
            //            }
            //        }
            //    }
            //}
            // catch (...)
            //{
            //}
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        void Send(std::string_view token, TMessage&& message, std::function<void(void)>&& send_completion_callback)
        {
            // try
            //{
            //     {
            //         auto find_it = m_routing_map.find(std::string(token));
            //         if (find_it == std::end(m_routing_map))
            //         {
            //             return;
            //         }

            //        for (const ComponentActionPair& recipient : find_it->second)
            //        {
            //            const auto& component = std::any_cast<ZEngine::WeakRef<TRecipient>>(recipient.first);
            //            if (!component.expired())
            //            {
            //                const auto& callback = std::any_cast<action_callback<TMessage>>(recipient.second);
            //                callback(message);
            //            }
            //        }
            //        if (send_completion_callback)
            //        {
            //            send_completion_callback();
            //        }
            //    }
            //}
            // catch (...)
            //{
            //}
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        std::future<void> SendAsync(std::string_view token, TMessage&& message)
        {
            std::unique_lock lock(m_mutex);
            {
                std::string routing_token = token.data();
                if (!m_routing_map.contains(routing_token))
                {
                    co_return;
                }

                ComponentActionCollection& actions = m_routing_map[routing_token];

                for (const ComponentActionPair& recipient : actions)
                {
                    TRecipient* component = reinterpret_cast<TRecipient*>(recipient.first);

                    if (component)
                    {
                        const action_callback& callback = recipient.second;
                        co_await callback(&message);
                    }
                }
            }
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        std::future<void> SendAsync(std::string_view token, TMessage&& message, std::function<void(void)>&& send_completion_callback)
        {
            // try
            //{
            //     std::unique_lock lock(m_mutex);
            //     {
            //         std::string routing_token = token.data();
            //         if (!m_routing_map.contains(routing_token))
            //         {
            //             return;
            //         }

            //        ComponentActionCollection& actions = m_routing_map[routing_token];

            //        for (const ComponentActionPair& recipient : actions)
            //        {
            //            TRecipient* component = reinterpret_cast<TRecipient*>(recipient.first);

            //            if (component)
            //            {
            //                action_callback* callback = reinterpret_cast<action_callback*>(recipient.second);
            //                co_await callback(message);
            //            }
            //        }
            //        if (send_completion_callback)
            //        {
            //            send_completion_callback();
            //        }
            //    }
            //}
            // catch (...)
            //{
            //}
        }

        template <typename TRecipient, typename TMessage>
        void Register(TRecipient* const recipient, std::string_view token, action_callback&& callback)
        {
            std::lock_guard lock(m_mutex);

            std::string         routing_token    = token.data();
            ComponentActionPair component_action = std::make_pair(recipient, std::move(callback));
            if (!m_routing_map.contains(routing_token))
            {
                m_routing_map[routing_token] = ComponentActionCollection{};
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
        inline static std::future<void> SendAsync(std::string_view token, TMessage&& message, std::function<void(void)>&& completion_callback)
        {
            co_await m_messenger->SendAsync<TRecipient, TMessage>(token, std::forward<TMessage>(message), std::move(completion_callback));
        }

        template <typename TRecipient, typename TMessage, typename = std::enable_if_t<std::is_base_of_v<EmptyMessage, TMessage>>>
        inline static std::future<void> SendAsync(std::string_view token, TMessage&& message)
        {
            co_await m_messenger->SendAsync<TRecipient, TMessage>(token, std::forward<TMessage>(message));
        }

        template <typename TRecipient, typename TMessage>
        inline static void Register(TRecipient* const recipient, std::string_view token, action_callback&& callback)
        {
            m_messenger->Register<TRecipient, TMessage>(recipient, token, std::move(callback));
        }

    private:
        inline static ZEngine::Ref<Messenger> m_messenger = std::make_shared<Messenger>();
    };

#define MESSENGER_REGISTER(TRecipient, TMessage, token, recipient_ptr, function_body)                                   \
    IMessenger::Register<TRecipient, TMessage>(recipient_ptr, token, [this](void* const message) -> std::future<void> { \
        auto message_ptr = reinterpret_cast<TMessage*>(message);                                                        \
        function_body;                                                                                                  \
    });
} // namespace Tetragrama::Messengers
