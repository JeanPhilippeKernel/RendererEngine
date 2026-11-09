#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/ZEngineDef.h>
#include <chrono>

namespace ZEngine::Profiling
{

    struct ProfilerSample
    {
        cstring  Name       = nullptr; // string literal; not heap-allocated
        uint64_t DurationNs = 0;       // nanosecond duration of the zone
        uint64_t FrameIndex = 0;       // which frame this sample belongs to
    };

    struct ProfilerValue
    {
        cstring  Name       = nullptr;
        float    Value      = 0;
        uint64_t FrameIndex = 0;
    };

    // Fixed-capacity ring buffer for CPU profiling samples.
    // Thread-safe for concurrent writers (multiple threads can call Record()).
    // Single-reader: DumpLastFrame is called from the render thread only.
    class ProfilerBuffer
    {
    public:
        static constexpr uint32_t CAPACITY = 4096; // max samples per frame

        // Write a completed sample. Thread-safe via atomic index.
        // Drops the sample silently if the buffer is full (rare in practice).
        static void               Record(const char* name, uint64_t duration_ns);

        // Capture preserves double precision (e.g. exact byte counts through
        // 2^53). The per-frame display remains a float plot. Thread-safe.
        static void               RecordValue(const char* name, double value);

        // Advance the frame counter. Call once per frame from the main thread
        // before any profiling work for the new frame.
        static void               BeginFrame();

        // Copy all samples recorded during the *previous* frame into `out`.
        // Safe to call from any thread as long as it does not race with BeginFrame.
        static void               DumpLastFrame(Core::Containers::Array<ProfilerSample>& out);

        // Same as DumpLastFrame but for value plots.
        static void               DumpLastFrameValues(Core::Containers::Array<ProfilerValue>& out);

        static uint64_t           CurrentFrameIndex();

        /// Opt-in bounded multi-frame capture for debugger/benchmark tooling.
        /// Start/stop/export require quiescent writers (e.g. a stopped debugger).
        /// Recording is allocation-free and never performs file I/O. Literal
        /// names must remain alive through export; prefix is copied at start.
        static constexpr uint32_t CAPTURE_CAPACITY = 262144;
        static bool               StartCapture(const char* prefix = "");
        /// Stops accepting samples at the first matching record at/after the
        /// deadline, without a debugger stop inside the measured interval.
        /// The same quiescent-writer start/export contract applies.
        static bool               StartTimedCapture(uint32_t duration_ms, const char* prefix = "");
        static void               StopCapture();
        static uint32_t           CaptureCount();
        static uint32_t           CaptureDropped();
        static bool               WriteCaptureCsv(const char* filename);

    private:
        static ProfilerSample         s_samples[CAPACITY];
        static ProfilerValue          s_values[CAPACITY];
        static PaddedAtomic<uint32_t> s_sample_write_pos;
        static PaddedAtomic<uint32_t> s_value_write_pos;
        static PaddedAtomic<uint64_t> s_frame_index;
        static uint32_t               s_last_frame_sample_count;
        static uint32_t               s_last_frame_value_count;
        // Snapshot of previous frame's data (copied atomically at BeginFrame).
        static ProfilerSample         s_last_samples[CAPACITY];
        static ProfilerValue          s_last_values[CAPACITY];
        struct CaptureRecord
        {
            const char* Name        = nullptr;
            uint64_t    TimestampNs = 0;
            double      Value       = 0;
            bool        IsValue     = false;
        };
        static CaptureRecord          s_capture[CAPTURE_CAPACITY];
        static PaddedAtomic<bool>     s_capture_enabled;
        static PaddedAtomic<uint32_t> s_capture_count;
        static char                   s_capture_prefix[64];
        static uint64_t               s_capture_deadline_ns;
        static void                   Capture(const char* name, double value, bool is_value);
    };

    // RAII scope guard for the lightweight (non-Tracy) profiling path.
    // Records elapsed nanoseconds into ProfilerBuffer on destruction.
    struct ScopeGuard
    {
        const char* Name;
        uint64_t    StartNs;

        explicit ScopeGuard(const char* name) : Name(name), StartNs(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {}

        ~ScopeGuard()
        {
            uint64_t end_ns = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            ProfilerBuffer::Record(Name, end_ns - StartNs);
        }

        ScopeGuard(const ScopeGuard&)            = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
    };

} // namespace ZEngine::Profiling
