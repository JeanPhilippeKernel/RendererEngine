#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

namespace ZEngine::ECS
{
    struct SceneRequestId
    {
        uint64_t Lifetime                                = 0;
        uint64_t Sequence                                = 0;

        bool     operator==(const SceneRequestId&) const = default;
        explicit operator bool() const noexcept
        {
            return Lifetime && Sequence;
        }
    };

    enum class SceneLoadOutcome : uint8_t
    {
        Pending,
        Committed,
        Cancelled,
        Superseded,
        Failed
    };
    enum class SceneLoadStage : uint8_t
    {
        None,
        Admission,
        Read,
        Assets,
        Preparation,
        Publication
    };
    enum class SceneLoadError : uint8_t
    {
        None,
        Invalidated,
        Source,
        Resources,
        Unavailable,
        UnsupportedCapability
    };
    enum class SceneCancelResult : uint8_t
    {
        Cancelled,
        TooLate,
        AlreadyTerminal,
        NotFound
    };

    struct SceneLoadResult
    {
        SceneRequestId   Request             = {};
        uint64_t         ExpectedGeneration  = 0;
        uint64_t         CommittedGeneration = 0;
        SceneLoadOutcome Outcome             = SceneLoadOutcome::Pending;
        SceneLoadStage   Stage               = SceneLoadStage::None;
        SceneLoadError   Error               = SceneLoadError::None;
        bool             CleanupPending      = false;
    };

    /// Main-thread, allocation-free request ledger. Hosts own immutable input and
    /// worker payloads; workers never access this ledger. Poll results after the
    /// publication guard, rather than retaining caller-owned callback pointers.
    /// A terminal result does NOT authorize freeing a worker/upload payload.
    class SceneLoadCoordinator final
    {
    public:
        static constexpr std::size_t ResultCapacity                  = 8;

        SceneLoadCoordinator()                                       = default;
        SceneLoadCoordinator(const SceneLoadCoordinator&)            = delete;
        SceneLoadCoordinator& operator=(const SceneLoadCoordinator&) = delete;

        /// Validate/snapshot host input before admission. Failure changes nothing,
        /// including the caller's output and previously admitted requests.
        bool                  Admit(uint64_t expected_generation, SceneRequestId* out) noexcept
        {
            if (!OnThread() || m_sealed || !m_lifetime || !out || !expected_generation || expected_generation == std::numeric_limits<uint64_t>::max() || m_sequence == std::numeric_limits<uint64_t>::max())
            {
                return false;
            }
            Record* available = nullptr;
            for (auto& record : m_records)
            {
                if (record.State == Phase::Committing)
                {
                    return false;
                }
                if (record.State == Phase::Free && !available)
                {
                    available = &record;
                }
            }
            if (!available)
            {
                return false;
            }
            for (auto& record : m_records)
            {
                if (record.State == Phase::Waiting || record.State == Phase::Preparing)
                {
                    Terminal(record, SceneLoadOutcome::Superseded);
                }
            }
            available->Result = {
                {m_lifetime, ++m_sequence},
                expected_generation
            };
            available->State = Phase::Waiting;
            *out             = available->Result.Request;
            return true;
        }

        /// Host calls only when worker/storage capacity permits. Even acknowledged
        /// cancelled work blocks another preparation until its cleanup completes.
        bool TryStart(SceneRequestId request) noexcept
        {
            auto* record = Find(request);
            if (!record || m_sealed || record->State != Phase::Waiting)
            {
                return false;
            }
            for (const auto& other : m_records)
            {
                if (other.Result.CleanupPending)
                {
                    return false;
                }
            }
            record->State                 = Phase::Preparing;
            record->Result.CleanupPending = true;
            return true;
        }

        SceneCancelResult Cancel(SceneRequestId request) noexcept
        {
            auto* record = Find(request);
            if (!record)
            {
                return SceneCancelResult::NotFound;
            }
            if (record->State == Phase::Committing || record->Result.Outcome == SceneLoadOutcome::Committed)
            {
                return SceneCancelResult::TooLate;
            }
            if (record->State == Phase::Terminal)
            {
                return SceneCancelResult::AlreadyTerminal;
            }
            Terminal(*record, SceneLoadOutcome::Cancelled);
            return SceneCancelResult::Cancelled;
        }

        bool Fail(SceneRequestId request, SceneLoadStage stage, SceneLoadError error) noexcept
        {
            auto* record = Find(request);
            if (!record || (record->State != Phase::Waiting && record->State != Phase::Preparing) || stage == SceneLoadStage::None || error == SceneLoadError::None)
            {
                return false;
            }
            record->Result.Stage = stage;
            record->Result.Error = error;
            Terminal(*record, SceneLoadOutcome::Failed);
            return true;
        }

        /// Enter only inside the publication guard, after all fallible preflight.
        bool BeginCommit(SceneRequestId request, uint64_t active_generation) noexcept
        {
            auto* record = Find(request);
            if (!record || m_sealed || record->State != Phase::Preparing || record->Result.ExpectedGeneration != active_generation || active_generation == std::numeric_limits<uint64_t>::max())
            {
                return false;
            }
            record->State = Phase::Committing;
            return true;
        }

        bool Commit(SceneRequestId request, uint64_t published_generation) noexcept
        {
            auto* record = Find(request);
            if (!record || record->State != Phase::Committing || published_generation != record->Result.ExpectedGeneration + 1)
            {
                return false;
            }
            record->Result.CommittedGeneration = published_generation;
            Terminal(*record, SceneLoadOutcome::Committed);
            return true;
        }

        bool Poll(SceneRequestId request, SceneLoadResult* out) const noexcept
        {
            const auto* record = Find(request);
            if (!record || record->Acknowledged || !out)
            {
                return false;
            }
            *out = record->Result;
            return true;
        }

        /// Internal lifecycle queries remain valid after result acknowledgement.
        bool Pending(SceneRequestId request) const noexcept
        {
            const auto* record = Find(request);
            return record && record->State != Phase::Terminal;
        }

        /// Roll back a start ONLY when submission failed and no worker/upload
        /// ever received the payload. The host must discard that payload first.
        bool DeferUnsubmitted(SceneRequestId request) noexcept
        {
            auto* record = Find(request);
            if (!record || record->State != Phase::Preparing)
            {
                return false;
            }
            record->State                 = Phase::Waiting;
            record->Result.CleanupPending = false;
            return true;
        }

        bool Acknowledge(SceneRequestId request) noexcept
        {
            auto* record = Find(request);
            if (!record || record->State != Phase::Terminal || record->Acknowledged)
            {
                return false;
            }
            record->Acknowledged = true;
            Reclaim(*record);
            return true;
        }

        /// Only after the host has joined/completed all users and freed payloads.
        bool FinishCleanup(SceneRequestId request) noexcept
        {
            auto* record = Find(request);
            if (!record || record->State != Phase::Terminal || !record->Result.CleanupPending)
            {
                return false;
            }
            record->Result.CleanupPending = false;
            Reclaim(*record);
            return true;
        }

        void Shutdown() noexcept
        {
            if (!OnThread())
            {
                return;
            }
            m_sealed = true;
            for (auto& record : m_records)
            {
                if (record.State == Phase::Waiting || record.State == Phase::Preparing)
                {
                    Terminal(record, SceneLoadOutcome::Cancelled);
                }
            }
        }

        /// Terminal result records may remain, but no asynchronous owner may.
        bool Drained() const noexcept
        {
            if (!OnThread())
            {
                return false;
            }
            for (const auto& record : m_records)
            {
                if (record.Result.CleanupPending || (record.State != Phase::Free && record.State != Phase::Terminal))
                {
                    return false;
                }
            }
            return true;
        }

    private:
        enum class Phase : uint8_t
        {
            Free,
            Waiting,
            Preparing,
            Committing,
            Terminal
        };
        struct Record
        {
            SceneLoadResult Result       = {};
            Phase           State        = Phase::Free;
            bool            Acknowledged = false;
        };

        static uint64_t NextLifetime() noexcept
        {
            static std::atomic<uint64_t> next{1};
            auto                         value = next.load(std::memory_order_relaxed);
            while (value != std::numeric_limits<uint64_t>::max())
            {
                if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                {
                    return value;
                }
            }
            return 0; // Exhausted identity space: never reuse a lifetime token.
        }
        bool OnThread() const noexcept
        {
            return m_thread == std::this_thread::get_id();
        }
        Record* Find(SceneRequestId request) noexcept
        {
            if (OnThread() && request && request.Lifetime == m_lifetime)
            {
                for (auto& record : m_records)
                {
                    if (record.State != Phase::Free && record.Result.Request == request)
                    {
                        return &record;
                    }
                }
            }
            return nullptr;
        }
        const Record* Find(SceneRequestId request) const noexcept
        {
            return const_cast<SceneLoadCoordinator*>(this)->Find(request);
        }
        static void Terminal(Record& record, SceneLoadOutcome outcome) noexcept
        {
            record.Result.Outcome = outcome;
            record.State          = Phase::Terminal;
        }
        static void Reclaim(Record& record) noexcept
        {
            if (record.Acknowledged && !record.Result.CleanupPending)
            {
                record = {};
            }
        }

        const std::thread::id              m_thread   = std::this_thread::get_id();
        const uint64_t                     m_lifetime = NextLifetime();
        uint64_t                           m_sequence = 0;
        bool                               m_sealed   = false;
        std::array<Record, ResultCapacity> m_records  = {};
    };
} // namespace ZEngine::ECS
