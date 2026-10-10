#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Profiling/ProfilerBuffer.h>

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

    void ProfilerBuffer::Record(const char* name, uint64_t duration_ns)
    {
        ZENGINE_VALIDATE_ASSERT(name != nullptr, "ProfilerBuffer::Record: name must not be null")
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
