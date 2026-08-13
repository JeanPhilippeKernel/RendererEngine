#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <cstring>

namespace ZEngine::Rendering::Scenes
{
    void RenderScene::SeqBeginWrite()
    {
        // Increment to odd → signals readers to spin.
        m_seq.value.fetch_add(1, std::memory_order_release);
    }

    void RenderScene::SeqEndWrite()
    {
        // Increment to even → data is stable again.
        m_seq.value.fetch_add(1, std::memory_order_release);
    }

    uint32_t RenderScene::AddMeshInstance(const uuids::uuid& uuid, const char* name)
    {
        uint32_t id = NextInstanceId++;

        SeqBeginWrite();

        MeshInstance inst;
        inst.Id        = id;
        inst.MeshUUID  = uuid;
        inst.Transform = Core::Maths::Identity<Core::Maths::Mat4f>();
        if (name)
            ::strncpy(inst.Name, name, sizeof(inst.Name) - 1);

        Instances.push(inst);

        SeqEndWrite();
        MarkInstancesDirty();
        return id;
    }

    void RenderScene::RemoveMeshInstance(uint32_t id)
    {
        SeqBeginWrite();

        for (uint32_t i = 0; i < Instances.size(); ++i)
        {
            if (Instances[i].Id == id)
            {
                Instances.erase(i);
                break;
            }
        }

        SeqEndWrite();
        MarkInstancesDirty();
    }

    void RenderScene::SetInstanceTransform(uint32_t id, const Core::Maths::Mat4f& t)
    {
        SeqBeginWrite();

        for (uint32_t i = 0; i < Instances.size(); ++i)
        {
            if (Instances[i].Id == id)
            {
                Instances[i].Transform = t;
                break;
            }
        }

        SeqEndWrite();
        MarkInstancesDirty();
    }

    void RenderScene::MarkInstancesDirty()
    {
        for (auto& flag : InstancesDirty)
            flag.value.store(true, std::memory_order_release);
    }

    void RenderScene::GetInstancesSnapshot(Core::Memory::ArenaAllocator* scratch, Core::Containers::Array<MeshInstance>& out) const
    {
        while (true)
        {
            // Wait until sequence is even (no write in progress).
            uint64_t seq1 = m_seq.value.load(std::memory_order_acquire);
            if (seq1 & 1u)
                continue; // writer active — spin

            out.init(scratch, Instances.size() + 1);
            for (uint32_t i = 0; i < Instances.size(); ++i)
                out.push(Instances[i]);

            std::atomic_thread_fence(std::memory_order_acquire);
            uint64_t seq2 = m_seq.value.load(std::memory_order_acquire);
            if (seq1 == seq2)
                return; // consistent snapshot

            // Writer modified data during our read — retry.
            out.clear();
        }
    }

} // namespace ZEngine::Rendering::Scenes
