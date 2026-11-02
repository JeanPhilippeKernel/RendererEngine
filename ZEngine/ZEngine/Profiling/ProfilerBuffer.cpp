#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Profiling/ProfilerBuffer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ZEngine::Profiling
{
    // clang-format off
    ProfilerSample         ProfilerBuffer::s_samples[ProfilerBuffer::CAPACITY]      = {};
    ProfilerValue          ProfilerBuffer::s_values[ProfilerBuffer::CAPACITY]       = {};
    PaddedAtomic<uint32_t> ProfilerBuffer::s_sample_write_pos                       = {};
    PaddedAtomic<uint32_t> ProfilerBuffer::s_value_write_pos                        = {};
    PaddedAtomic<uint64_t> ProfilerBuffer::s_frame_index                            = {};
    uint32_t               ProfilerBuffer::s_last_frame_sample_count                = 0;
    uint32_t               ProfilerBuffer::s_last_frame_value_count                 = 0;
    ProfilerSample         ProfilerBuffer::s_last_samples[ProfilerBuffer::CAPACITY] = {};
    ProfilerValue          ProfilerBuffer::s_last_values[ProfilerBuffer::CAPACITY]  = {};
    // clang-format on
    ProfilerBuffer::CaptureRecord ProfilerBuffer::s_capture[ProfilerBuffer::CAPTURE_CAPACITY] = {};
    PaddedAtomic<bool>            ProfilerBuffer::s_capture_enabled                           = {};
    PaddedAtomic<uint32_t>        ProfilerBuffer::s_capture_count                             = {};
    char                          ProfilerBuffer::s_capture_prefix[64]                        = {};
    uint64_t                      ProfilerBuffer::s_capture_deadline_ns                       = 0;

    bool                          ProfilerBuffer::StartCapture(const char* prefix)
    {
        if (!prefix || std::strlen(prefix) >= sizeof(s_capture_prefix) || s_capture_enabled.value.load(std::memory_order_acquire))
            return false;
        Helpers::secure_strcpy(s_capture_prefix, sizeof(s_capture_prefix), prefix);
        s_capture_deadline_ns = 0;
        s_capture_count.value.store(0, std::memory_order_relaxed);
        s_capture_enabled.value.store(true, std::memory_order_release);
        return true;
    }

    bool ProfilerBuffer::StartTimedCapture(uint32_t duration_ms, const char* prefix)
    {
        if (duration_ms == 0 || !StartCapture(prefix))
            return false;
        s_capture_deadline_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) + static_cast<uint64_t>(duration_ms) * 1'000'000ull;
        return true;
    }

    void ProfilerBuffer::StopCapture()
    {
        s_capture_enabled.value.store(false, std::memory_order_release);
    }

    uint32_t ProfilerBuffer::CaptureCount()
    {
        return std::min(s_capture_count.value.load(std::memory_order_acquire), CAPTURE_CAPACITY);
    }

    uint32_t ProfilerBuffer::CaptureDropped()
    {
        const uint32_t count = s_capture_count.value.load(std::memory_order_acquire);
        return count > CAPTURE_CAPACITY ? count - CAPTURE_CAPACITY : 0;
    }

    void ProfilerBuffer::Capture(const char* name, double value, bool is_value)
    {
        if (!s_capture_enabled.value.load(std::memory_order_acquire) || std::strncmp(name, s_capture_prefix, std::strlen(s_capture_prefix)) != 0)
            return;
        const auto now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (s_capture_deadline_ns && now_ns >= s_capture_deadline_ns)
        {
            s_capture_enabled.value.store(false, std::memory_order_release);
            return;
        }
        const auto index = s_capture_count.value.fetch_add(1, std::memory_order_relaxed);
        if (index < CAPTURE_CAPACITY)
            s_capture[index] = {name, now_ns, value, is_value};
    }

    bool ProfilerBuffer::WriteCaptureCsv(const char* filename)
    {
        if (s_capture_enabled.value.load(std::memory_order_acquire) || !filename || !filename[0])
            return false;
        auto* file = std::fopen(filename, "wb");
        if (!file)
            return false;
        bool success = std::fprintf(file, "kind,name,timestamp_ns,value\n") > 0;
        for (uint32_t i = 0; i < CaptureCount() && success; ++i)
        {
            const auto& record = s_capture[i];
            success            = std::fprintf(file, "%s,\"", record.IsValue ? "value" : "scope_ns") > 0;
            for (const char* c = record.Name; *c && success; ++c)
            {
                if (*c == '"')
                    success = std::fputc('"', file) != EOF;
                if (success)
                    success = std::fputc(*c, file) != EOF;
            }
            if (success)
                success = std::fprintf(file, "\",%llu,%.17g\n", static_cast<unsigned long long>(record.TimestampNs), record.Value) > 0;
        }
        return std::fclose(file) == 0 && success;
    }

    void ProfilerBuffer::Record(const char* name, uint64_t duration_ns)
    {
        ZENGINE_VALIDATE_ASSERT(name != nullptr, "ProfilerBuffer::Record: name must not be null")
        Capture(name, static_cast<double>(duration_ns), false);
        auto write_pos = s_sample_write_pos.value.fetch_add(1, std::memory_order_relaxed);
        if (write_pos < CAPACITY)
        {
            auto frame_index     = s_frame_index.value.load(std::memory_order_acquire);
            s_samples[write_pos] = {name, duration_ns, frame_index};
        }
    }

    void ProfilerBuffer::RecordValue(cstring name, float value)
    {
        ZENGINE_VALIDATE_ASSERT(name != nullptr, "ProfilerBuffer::RecordValue: name must not be null")
        Capture(name, value, true);
        auto write_pos = s_value_write_pos.value.fetch_add(1, std::memory_order_relaxed);
        if (write_pos < CAPACITY)
        {
            auto frame_index    = s_frame_index.value.load(std::memory_order_acquire);
            s_values[write_pos] = {name, value, frame_index};
        }
    }

    void ProfilerBuffer::BeginFrame()
    {
        uint32_t sample_count     = s_sample_write_pos.value.exchange(0, std::memory_order_seq_cst);
        uint32_t value_count      = s_value_write_pos.value.exchange(0, std::memory_order_seq_cst);

        s_last_frame_sample_count = sample_count < CAPACITY ? sample_count : CAPACITY;
        s_last_frame_value_count  = value_count < CAPACITY ? value_count : CAPACITY;

        Helpers::secure_memcpy(s_last_samples, CAPACITY * sizeof(ProfilerSample), s_samples, s_last_frame_sample_count * sizeof(ProfilerSample));
        Helpers::secure_memcpy(s_last_values, CAPACITY * sizeof(ProfilerValue), s_values, s_last_frame_value_count * sizeof(ProfilerValue));

        s_frame_index.value.fetch_add(1, std::memory_order_relaxed);
    }

    void ProfilerBuffer::DumpLastFrame(Core::Containers::Array<ProfilerSample>& out)
    {
        out.clear();
        for (uint32_t i = 0; i < s_last_frame_sample_count; ++i)
        {
            out.push(s_last_samples[i]);
        }
    }

    void ProfilerBuffer::DumpLastFrameValues(Core::Containers::Array<ProfilerValue>& out)
    {
        out.clear();
        for (uint32_t i = 0; i < s_last_frame_value_count; ++i)
        {
            out.push(s_last_values[i]);
        }
    }

    uint64_t ProfilerBuffer::CurrentFrameIndex()
    {
        return s_frame_index.value.load(std::memory_order_relaxed);
    }

} // namespace ZEngine::Profiling
