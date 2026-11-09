#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS
{
    // Stable machine-readable failure categories; explanatory text is not an API.
    enum class SceneDiagnosticCode : uint8_t
    {
        None = 0,
        InvalidArgument,
        InvalidData,
        UnsupportedVersion,
        LimitExceeded,
        AllocationFailed,
        UnsupportedCapability,
    };

    enum class SceneDiagnosticSeverity : uint8_t
    {
        Info,
        Warning,
        Error,
    };

    struct SceneDiagnostics
    {
        static constexpr uint32_t MAX_ENTRIES     = 64;
        static constexpr uint32_t MAX_MESSAGE_LEN = 192;

        struct Entry
        {
            SceneDiagnosticSeverity Severity                 = SceneDiagnosticSeverity::Error;
            SceneDiagnosticCode     Code                     = SceneDiagnosticCode::None;
            char                    Message[MAX_MESSAGE_LEN] = {};
        };

        Entry    Entries[MAX_ENTRIES] = {};
        uint32_t Count                = 0;
        uint32_t Dropped              = 0;
        uint32_t ErrorCount           = 0;
        // Retains categories even when bounded entry storage overflows.
        uint32_t ErrorCodes           = 0;

        void     Add(SceneDiagnosticSeverity severity, cstring message, SceneDiagnosticCode code = SceneDiagnosticCode::None)
        {
            if (auto* entry = Retain(severity, code))
            {
                Helpers::secure_strncpy(entry->Message, MAX_MESSAGE_LEN, message ? message : "", MAX_MESSAGE_LEN - 1);
            }
        }

        // Write directly into retained caller storage without a temporary record
        // or message buffer. The optional initial message may alias retained
        // storage; unused message bytes are cleared in place. A full
        // buffer still counts errors/categories and drops, but returns null.
        // The slot is invalidated by Clear/reuse; keep Severity/Code unchanged
        // after retention because the counters already reflect them.
        [[nodiscard]] Entry* Emplace(SceneDiagnosticSeverity severity, SceneDiagnosticCode code = SceneDiagnosticCode::None, cstring message = nullptr)
        {
            auto* entry = Retain(severity, code);
            if (entry)
            {
                uint32_t length = 0;
                while (message && length < MAX_MESSAGE_LEN - 1 && message[length])
                {
                    ++length;
                }
                if (length)
                {
                    Helpers::secure_memmove(entry->Message, MAX_MESSAGE_LEN, message, length);
                }
                Helpers::secure_memset(entry->Message + length, 0, MAX_MESSAGE_LEN - length, MAX_MESSAGE_LEN - length);
            }
            return entry;
        }

        void Error(cstring message, SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
        {
            Add(SceneDiagnosticSeverity::Error, message, code);
        }

        [[nodiscard]] bool HasErrors() const
        {
            return ErrorCount > 0;
        }

        [[nodiscard]] bool HasError(SceneDiagnosticCode code) const
        {
            return (ErrorCodes & (uint32_t{1} << static_cast<uint8_t>(code))) != 0;
        }

        void Clear()
        {
            Count      = 0;
            Dropped    = 0;
            ErrorCount = 0;
            ErrorCodes = 0;
        }

    private:
        [[nodiscard]] Entry* Retain(SceneDiagnosticSeverity severity, SceneDiagnosticCode code)
        {
            if (severity == SceneDiagnosticSeverity::Error)
            {
                ++ErrorCount;
                ErrorCodes |= uint32_t{1} << static_cast<uint8_t>(code);
            }

            if (Count >= MAX_ENTRIES)
            {
                ++Dropped;
                return nullptr;
            }

            Entries[Count].Severity = severity;
            Entries[Count].Code     = code;
            return &Entries[Count++];
        }
    };

    inline void SceneDiagnosticError(SceneDiagnostics* diagnostics, cstring message, SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
    {
        if (diagnostics)
        {
            diagnostics->Error(message, code);
        }
    }
} // namespace ZEngine::ECS
