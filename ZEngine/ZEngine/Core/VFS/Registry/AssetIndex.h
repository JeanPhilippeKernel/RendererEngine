#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Helpers/HandleManager.h>
#include <shared_mutex>
#include <span>

namespace ZEngine::Core::VFS
{
    // FNV-1a hasher for uuids::uuid — used by DependencyGraph maps and the cascade BFS.
    struct UUIDHasher
    {
        uint64_t operator()(const uuids::uuid& id) const noexcept
        {
            constexpr uint64_t FNV_BASIS = 14695981039346656037ULL;
            constexpr uint64_t FNV_PRIME = 1099511628211ULL;
            const auto         bytes     = id.as_bytes();
            uint64_t           h         = FNV_BASIS;
            for (auto b : bytes)
            {
                h ^= static_cast<uint8_t>(b);
                h *= FNV_PRIME;
            }
            return h;
        }
        bool operator()(const uuids::uuid& a, const uuids::uuid& b) const noexcept
        {
            return a == b;
        }
    };

    enum class RegisterError : uint8_t
    {
        None          = 0,
        DuplicateUUID = 1,
        DuplicatePath = 2,
        SlotExhausted = 3,
    };

    struct RegisterResult
    {
        Helpers::Handle<AssetRecord> Handle = {};
        RegisterError                Error  = RegisterError::None;

        bool                         IsOk() const
        {
            return Error == RegisterError::None && Handle.Valid();
        }
    };

    class AssetIndex
    {
    public:
        static constexpr uint32_t MAX_ASSETS       = 8192;
        static constexpr uint8_t  ASSET_TYPE_COUNT = 4;

        AssetIndex()                               = default;
        ~AssetIndex()                              = default;

        void                                          Initialize(Core::Memory::ArenaAllocator* arena);

        // Mutation
        RegisterResult                                Register(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta);

        bool                                          Update(Helpers::Handle<AssetRecord> handle, const Core::VFS::MetaFileData& new_meta, AssetState new_state);

        bool                                          SetState(Helpers::Handle<AssetRecord> handle, AssetState new_state);

        bool                                          Remove(Helpers::Handle<AssetRecord> handle);

        bool                                          Rename(Helpers::Handle<AssetRecord> handle, const Core::VFS::VFSPath& new_path);

        // Lookup
        AssetRecord*                                  Access(Helpers::Handle<AssetRecord> handle);
        const AssetRecord*                            Access(Helpers::Handle<AssetRecord> handle) const;

        Helpers::Handle<AssetRecord>                  FindByUUID(const uuids::uuid& uuid) const;
        Helpers::Handle<AssetRecord>                  FindByPath(const Core::VFS::VFSPath& path) const;

        std::span<const Helpers::Handle<AssetRecord>> FindByType(Managers::AssetType type) const;

        uint32_t                                      FindByNameSubstring(const char* substr, Core::Containers::Array<Helpers::Handle<AssetRecord>>& out) const;

        bool                                          IsLive(Helpers::Handle<AssetRecord> handle) const;

        void                                          ForEach(void* ctx, void (*visitor)(void*, Helpers::Handle<AssetRecord>, const AssetRecord&)) const;

        uint32_t                                      Count() const;
        uint32_t                                      Capacity() const;

    private:
        Helpers::HandleManager<AssetRecord>                                           m_handles                   = {};

        // uuid is 16 bytes — rapidhash(&uuid, 16) gives a stable hash.
        Core::Containers::UnorderedHashMap<uuids::uuid, Helpers::Handle<AssetRecord>> m_by_uuid                   = {};
        // VFSPath is large and has padding — key on its stable uint64_t hash instead.
        Core::Containers::UnorderedHashMap<uint64_t, Helpers::Handle<AssetRecord>>    m_by_path                   = {};

        Core::Containers::Array<Helpers::Handle<AssetRecord>>                         m_by_type[ASSET_TYPE_COUNT] = {};

        mutable std::shared_mutex                                                     m_mutex                     = {};
        Core::Memory::ArenaAllocator*                                                 m_arena                     = nullptr;
    };

} // namespace ZEngine::Core::VFS
