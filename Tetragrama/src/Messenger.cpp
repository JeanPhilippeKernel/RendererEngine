#include <pch.h>
#include <Messenger.h>

namespace Tetragrama {
    template<typename TMessage>
    void Messenger::Send(std::string_view token, TMessage&& message)
    {
        auto find_it = m_routing_map.find(token);
        if (find_it == std::end(m_routing_map)) {
            return;
        }
        auto registred_callback_collection = find_it->second;
        for (const auto& callback : registred_callback_collection)
        {
            callback(message);
        }
    }

    template<typename TRecipient, typename TMessage>
    void Messenger::Register(TRecipient recipient, std::string_view token, action_callback callback)
    {
        m_routing_map[token] = 
    }

    template<typename TRecipient, typename TMessage>
    void Messenger::Register(TRecipient* recipient, void* token, action_callback callback)
    {

    }

}