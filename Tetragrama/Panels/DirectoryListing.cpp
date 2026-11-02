#include <Tetragrama/Panels/DirectoryListing.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Profiling/Profiling.h>
#include <algorithm>
#include <cstring>
#include <new>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

namespace Tetragrama::Panels
{
    namespace
    {
        // Numeric runs compare without integer parsing/overflow. Case/zero ties
        // use exact bytes, independent of backend enumeration order.
        int NaturalNameCompare(const char* a, const char* b)
        {
            const auto  digit = [](char c) { return c >= '0' && c <= '9'; };
            const auto  lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            const char* left  = a;
            const char* right = b;
            while (*left && *right)
            {
                if (digit(*left) && digit(*right))
                {
                    while (*left == '0')
                        ++left;
                    while (*right == '0')
                        ++right;
                    const char* ld = left;
                    const char* rd = right;
                    while (digit(*left))
                        ++left;
                    while (digit(*right))
                        ++right;
                    const auto ll = left - ld;
                    const auto rl = right - rd;
                    if (ll != rl)
                        return ll < rl ? -1 : 1;
                    for (std::ptrdiff_t i = 0; i < ll; ++i)
                        if (ld[i] != rd[i])
                            return ld[i] < rd[i] ? -1 : 1;
                    continue;
                }
                const auto lc = lower(static_cast<unsigned char>(*left));
                const auto rc = lower(static_cast<unsigned char>(*right));
                if (lc != rc)
                    return lc < rc ? -1 : 1;
                ++left;
                ++right;
            }
            if (*left || *right)
                return *left ? 1 : -1;
            return ZEngine::Helpers::secure_strcmp(a, b);
        }
    } // namespace

    struct DirectoryListing::Work
    {
        ZEngine::Core::VFS::IVFSContext*      VFS             = nullptr;
        ZEngine::Core::VFS::VFSPath           Path            = {};
        uint64_t                              Generation      = 0;
        bool                                  DirectoriesOnly = false;
        std::atomic<bool>                     Cancelled       = false;
        std::atomic<bool>                     Done            = false;
        ZEngine::Core::Memory::ArenaAllocator Scratch{};
        std::unique_ptr<Result>               Output{};
    };

    DirectoryListing::DirectoryListing() = default;

    DirectoryListing::~DirectoryListing()
    {
        ZENGINE_VALIDATE_ASSERT(!m_pending || m_work->Done.load(std::memory_order_acquire), "Directory listing workers must join before owner destruction")
    }

    bool DirectoryListing::Submit(void*, void* work, void (*run)(void*))
    {
        return ZEngine::Helpers::ThreadPoolHelper::Submit(work, run);
    }

    void DirectoryListing::SetSubmitter(Submitter submit, void* context)
    {
        ZENGINE_VALIDATE_ASSERT(!m_pending, "Cannot replace a directory executor with work in flight")
        m_submit         = submit ? submit : &Submit;
        m_submit_context = context;
    }

    void DirectoryListing::Invalidate()
    {
        ++m_generation;
        m_requested = false;
        m_status    = Status::Idle;
        if (m_pending)
            m_work->Cancelled.store(true, std::memory_order_release);
    }

    void DirectoryListing::Request(ZEngine::Core::VFS::IVFSContext& vfs, const ZEngine::Core::VFS::VFSPath& path, bool directories_only)
    {
        if (m_closed)
            return;
        Invalidate();
        m_vfs              = &vfs;
        m_path             = path;
        m_directories_only = directories_only;
        m_requested        = true;
        m_status           = Status::Loading;
    }

    const DirectoryListing::Result* DirectoryListing::Poll()
    {
        if (m_closed)
            return nullptr;
        if (m_pending)
        {
            if (!m_work->Done.load(std::memory_order_acquire))
                return nullptr;
            m_pending = false;
            if (m_work->Generation == m_generation)
            {
                m_status = m_work->Output && m_work->Output->Succeeded ? Status::Ready : Status::Failed;
                return m_work->Output.get();
            }
        }
        if (!m_requested)
            return nullptr;
        if (!m_work)
            m_work.reset(new (std::nothrow) Work{});
        if (!m_work)
        {
            m_requested = false;
            m_status    = Status::Failed;
            return nullptr;
        }
        auto& work           = *m_work;
        work.VFS             = m_vfs;
        work.Path            = m_path;
        work.Generation      = m_generation;
        work.DirectoriesOnly = m_directories_only;
        work.Cancelled.store(false, std::memory_order_relaxed);
        work.Done.store(false, std::memory_order_relaxed);
        // Publish all context before admission (test executors may complete now).
        if (m_submit(m_submit_context, &work, &Run))
        {
            m_pending   = true;
            m_requested = false;
            if (work.Done.load(std::memory_order_acquire))
                return Poll();
        }
        return nullptr; // Queue full/unavailable: retry next frame, never inline.
    }

    void DirectoryListing::Run(void* context)
    {
        ZENGINE_PROFILE_SCOPE("ContentBrowser.ListWorker");
        auto& work = *static_cast<Work*>(context);
        // Large result storage is initialized on the worker, not the first UI
        // frame. No per-refresh allocation after this channel has warmed up.
        if (!work.Output)
            work.Output.reset(new (std::nothrow) Result{});
        if (!work.Output)
        {
            work.Done.store(true, std::memory_order_release);
            return;
        }
        work.Output->Count     = 0;
        work.Output->Truncated = work.Output->Succeeded = false;
        if (!work.Cancelled.load(std::memory_order_acquire))
        {
            // Task-owned scratch, never the engine/UI frame arena. Reused only
            // after the previous release/acquire completion handshake.
            if (!work.Scratch.m_memory)
                work.Scratch.Initialize(ZMega(64), 0, "ContentBrowser/DirectoryListing");
            if (work.Scratch.m_memory)
            {
                work.Scratch.Clear();
                auto raw = work.VFS->List(work.Path, &work.Scratch);
                if (raw.Succeeded() && !work.Cancelled.load(std::memory_order_acquire))
                {
                    Prepare(raw.Value(), work.Output->Entries, Capacity, work.Output->Count, work.Output->Truncated, work.DirectoriesOnly, &work.Path);
                    work.Output->Succeeded = true;
                }
            }
        }
        work.Done.store(true, std::memory_order_release); // Last worker access.
    }

    void DirectoryListing::Close()
    {
        Invalidate();
        m_closed = true;
    }

    void DirectoryListing::ShutdownAfterWorkersJoined()
    {
        Close();
        // Also reclaim accepted requests the pool discarded without starting.
        m_pending = false;
        m_work.reset();
        m_vfs = nullptr;
    }

    bool DirectoryListing::EntryLess(const DirectoryEntry& a, const DirectoryEntry& b)
    {
        if (a.is_dir != b.is_dir)
            return a.is_dir;
        const int order = NaturalNameCompare(a.name, b.name);
        return order ? order < 0 : ZEngine::Helpers::secure_strcmp(a.full_path, b.full_path) < 0;
    }

    void DirectoryListing::Prepare(const ZEngine::Core::Containers::Array<ZEngine::Core::VFS::VFSDirEntry>& raw, DirectoryEntry* output, uint32_t capacity, uint32_t& count, bool& truncated, bool directories_only, const ZEngine::Core::VFS::VFSPath* parent)
    {
        ZENGINE_PROFILE_SCOPE("ContentBrowser.PrepareListing");
        count     = 0;
        truncated = false;
        for (const auto& entry : raw)
        {
            const auto name = entry.Path.Filename();
            if (!entry.Path.IsValid() || name.Empty() || name.Data[0] == '.' || (parent && entry.Path.Parent() != *parent))
                continue;
            if (directories_only && !entry.IsDirectory)
                continue;
            const auto extension = entry.Path.Extension();
            if (!entry.IsDirectory && extension.Data && strcasecmp(extension.Data, ".meta") == 0)
                continue;
            if (count == capacity)
            {
                truncated = true;
                break;
            }
            auto& cached = output[count++];
            ZEngine::Helpers::secure_memcpy(cached.name, sizeof(cached.name), name.Data, name.Length);
            cached.name[name.Length] = '\0';
            ZEngine::Helpers::secure_memcpy(cached.full_path, sizeof(cached.full_path), entry.Path.CStr(), entry.Path.Length() + 1);
            cached.is_dir = entry.IsDirectory;
        }
        std::sort(output, output + count, EntryLess);
    }
} // namespace Tetragrama::Panels
