#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace ZEngine::Windows
{
    enum class FileDialogStatus : uint8_t
    {
        Pending,
        Selected,
        Cancelled,
        Failed,
    };

    struct FileDialogResult
    {
        FileDialogStatus Outcome = FileDialogStatus::Failed;
        std::string      Path    = {};
    };

    /// Single-consumer, single-completion request. Native completion owns a copy
    /// of the state, never an editor/window pointer. Polling does not block and
    /// abandoning the consumer neither blocks nor invalidates the producer.
    class FileDialogRequest
    {
    public:
        static FileDialogRequest Create()
        {
            FileDialogRequest request;
            request.m_state = std::make_shared<State>();
            return request;
        }

        bool valid() const
        {
            return m_state != nullptr;
        }

        bool TryTake(FileDialogResult* out)
        {
            if (!out || !m_state || !m_state->Ready.load(std::memory_order_acquire))
            {
                return false;
            }
            *out = std::move(m_state->Result);
            m_state.reset();
            return true;
        }

        bool Complete(std::string path) const
        {
            const auto status = path.empty() ? FileDialogStatus::Cancelled : FileDialogStatus::Selected;
            return Publish({status, std::move(path)});
        }
        bool Fail() const
        {
            return Publish({});
        }

    private:
        struct State
        {
            std::atomic<bool> Claimed = false;
            std::atomic<bool> Ready   = false;
            FileDialogResult  Result  = {};
        };
        std::shared_ptr<State> m_state = {};

        bool                   Publish(FileDialogResult result) const
        {
            if (!m_state || m_state->Claimed.exchange(true, std::memory_order_acq_rel))
            {
                return false;
            }
            m_state->Result = std::move(result);
            m_state->Ready.store(true, std::memory_order_release);
            return true;
        }
    };
} // namespace ZEngine::Windows
