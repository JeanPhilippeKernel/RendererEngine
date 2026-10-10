#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS
{
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
            char                    Message[MAX_MESSAGE_LEN] = {};
        };

        Entry    Entries[MAX_ENTRIES] = {};
        uint32_t Count                = 0;
        uint32_t Dropped              = 0;
        uint32_t ErrorCount           = 0;

        void     Add(SceneDiagnosticSeverity severity, cstring message)
        {
            if (severity == SceneDiagnosticSeverity::Error)
            {
                ++ErrorCount;
            }

            if (Count >= MAX_ENTRIES)
            {
                ++Dropped;
                return;
            }

            Entries[Count].Severity = severity;
            Helpers::secure_strncpy(Entries[Count].Message, MAX_MESSAGE_LEN, message ? message : "", MAX_MESSAGE_LEN - 1);
            ++Count;
        }

        void Error(cstring message)
        {
            Add(SceneDiagnosticSeverity::Error, message);
        }

        [[nodiscard]] bool HasErrors() const
        {
            return ErrorCount > 0;
        }

        void Clear()
        {
            Count      = 0;
            Dropped    = 0;
            ErrorCount = 0;
        }
    };

    inline void SceneDiagnosticError(SceneDiagnostics* diagnostics, cstring message)
    {
        if (diagnostics)
        {
            diagnostics->Error(message);
        }
    }
} // namespace ZEngine::ECS
