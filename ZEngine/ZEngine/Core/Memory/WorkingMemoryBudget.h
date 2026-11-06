#pragma once
#include <cstddef>

namespace ZEngine::Core::Memory
{
    // Borrowed synchronous budget for fixed working storage outside allocated
    // blocks. No allocation, ownership graph or pool-containment inference.
    // A successful reservation remains charged until the matching release.
    class IWorkingMemoryBudget
    {
    public:
        virtual ~IWorkingMemoryBudget()                                          = default;
        [[nodiscard]] virtual bool TryReserveWorkingBytes(size_t bytes) noexcept = 0;
        virtual void               ReleaseWorkingBytes(size_t bytes) noexcept    = 0;
    };

    // Optional accounting for shared code with budgetless editor callers. Cook
    // routes always supply a budget; a null budget is not a cooker fallback.
    // The budget outlives this reservation. Construct fixed storage after
    // acquisition and drain it before releasing the reservation.
    class WorkingMemoryCharge
    {
    public:
        WorkingMemoryCharge() = default;
        ~WorkingMemoryCharge()
        {
            Release();
        }
        WorkingMemoryCharge(const WorkingMemoryCharge&)            = delete;
        WorkingMemoryCharge& operator=(const WorkingMemoryCharge&) = delete;

        [[nodiscard]] bool   TryAcquire(IWorkingMemoryBudget* budget, size_t bytes) noexcept
        {
            if (m_budget)
            {
                return false;
            }
            if (!budget || !bytes)
            {
                return true;
            }
            if (!budget->TryReserveWorkingBytes(bytes))
            {
                return false;
            }
            m_budget = budget;
            m_bytes  = bytes;
            return true;
        }
        void Release() noexcept
        {
            if (m_budget)
            {
                m_budget->ReleaseWorkingBytes(m_bytes);
            }
            m_budget = nullptr;
            m_bytes  = 0;
        }

    private:
        IWorkingMemoryBudget* m_budget = nullptr;
        size_t                m_bytes  = 0;
    };
} // namespace ZEngine::Core::Memory
