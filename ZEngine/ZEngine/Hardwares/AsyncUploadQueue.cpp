#include <ZEngine/Hardwares/AsyncUploadQueue.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Logging/LoggerDefinition.h>

namespace ZEngine::Hardwares
{
    void AsyncUploadQueue::Initialize(VulkanDevice* device)
    {
        m_device = device;
    }

    void AsyncUploadQueue::Deinitialize()
    {
        Clear();
        m_device = nullptr;
    }

    void AsyncUploadQueue::Enqueue(const AsyncUploadJob& job)
    {
        if (!m_jobs.push(job))
        {
            ZENGINE_CORE_WARN("[AsyncUploadQueue] Queue full ({} jobs) — dropping upload job", CAPACITY)
        }
    }

    void AsyncUploadQueue::SubmitAll()
    {
        AsyncUploadJob job;
        while (m_jobs.pop(job))
        {
            // Stop draining once the device is lost rather than keep submitting every
            // remaining job — those would just add misleading cascade errors on top of the
            // real failure, and the device may already be unsafe to keep calling into.
            if (!m_device->QueueSubmit(job.Buffer, job.Timeline, job.WaitFlag, job.SignalValue, job.WaitValue, job.WaitTimeline))
                break;
            m_device->EnqueueAsyncGPUOperation({job.WaitFlag, job.SignalValue, job.Timeline});
        }
    }

    void AsyncUploadQueue::Clear()
    {
        AsyncUploadJob job;
        while (m_jobs.pop(job))
        {
        }
    }
} // namespace ZEngine::Hardwares
