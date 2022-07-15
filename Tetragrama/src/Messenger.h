#pragma once
#include <unordered_map>
#include <functional>
#include <string_view>

namespace Tetragrama
{
    class Messenger
    {
        using action_callback = std::function<void(void* const recipient_ptr, void* const message_ptr)>;

    public:
        template<typename TMessage>
        void Send(std::string_view token, TMessage&& message);

        template<typename TRecipient, typename TMessage>
        void Register(TRecipient recipient, std::string_view token, action_callback callback);

        template<typename TRecipient, typename TMessage>
        void Register(TRecipient* recipient, void* token, action_callback callback);

    private:
        std::unordered_map<std::string, std::vector<action_callback>> m_routing_map;
    };
    
} // namespace Tetragrama
