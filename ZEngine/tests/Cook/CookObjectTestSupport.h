#pragma once
#include <ZEngine/Cook/CookObjectReader.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>

namespace ZEngine::Cook::Tests
{
    struct ObjectFile final : Core::VFS::IVFSFile
    {
        Core::Containers::ArrayView<const uint8_t> Bytes            = {};
        Core::VFS::VFSPath                         Name             = {};
        size_t                                     ReadLimit        = SIZE_MAX;
        uint64_t                                   EOFAt            = UINT64_MAX;
        uint64_t                                   ReadFailureAt    = UINT64_MAX;
        uint64_t                                   ChangedSize      = UINT64_MAX;
        Core::VFS::VFSError                        ReadError        = Core::VFS::VFSError::PermissionDenied;
        Core::VFS::VFSError                        StatError        = Core::VFS::VFSError::OK;
        Core::VFS::VFSError                        CloseError       = Core::VFS::VFSError::OK;
        uint32_t                                   StatFailureAt    = 0;
        uint32_t                                   Reads            = 0;
        uint32_t                                   Closes           = 0;
        mutable uint32_t                           Stats            = 0;
        bool                                       Overreport       = false;
        bool                                       Directory        = false;
        bool                                       ChangedDirectory = false;

        Core::VFS::VFSResult<size_t>               Read(Core::Containers::ArrayView<uint8_t> destination, uint64_t offset) override
        {
            ++Reads;
            if (offset >= ReadFailureAt)
            {
                return Core::VFS::VFSResult<size_t>::Fail(ReadError);
            }
            if (Overreport)
            {
                return Core::VFS::VFSResult<size_t>::Ok(destination.size() + 1);
            }
            const uint64_t end   = std::min<uint64_t>(Bytes.size(), EOFAt);
            const size_t   count = offset >= end ? 0 : std::min({destination.size(), ReadLimit, static_cast<size_t>(end - offset)});
            if (count)
            {
                (void) Helpers::secure_memcpy(destination.data(), destination.size(), Bytes.data() + offset, count);
            }
            return Core::VFS::VFSResult<size_t>::Ok(count);
        }
        Core::VFS::VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t>, uint64_t) override
        {
            ADD_FAILURE() << "Object verification must not write.";
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::PermissionDenied);
        }
        Core::VFS::VFSResult<uint64_t> Size() const override
        {
            return Core::VFS::VFSResult<uint64_t>::Ok(Bytes.size());
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileStat> Stat() const override
        {
            ++Stats;
            if (StatError != Core::VFS::VFSError::OK && (!StatFailureAt || StatFailureAt == Stats))
            {
                return Core::VFS::VFSResult<Core::VFS::VFSFileStat>::Fail(StatError);
            }
            return Core::VFS::VFSResult<Core::VFS::VFSFileStat>::Ok({.SizeBytes = Stats > 1 && ChangedSize != UINT64_MAX ? ChangedSize : Bytes.size(), .IsDirectory = Directory || (Stats > 1 && ChangedDirectory)});
        }
        Core::VFS::VFSResult<void> Flush() override
        {
            ADD_FAILURE() << "Object verification must not flush.";
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::PermissionDenied);
        }
        const Core::VFS::VFSPath& Path() const override
        {
            return Name;
        }
        Core::VFS::VFSResult<void> Close() override
        {
            ++Closes;
            return CloseError == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(CloseError);
        }
    };

    // Bounded inline store and fault injection, shared by verifier/reuse tests.
    // It deliberately has no mutation/publication callbacks.
    struct ObjectStore
    {
        struct Entry
        {
            uint8_t              Bytes[512] = {};
            CookObjectDescriptor Object     = {};
            ObjectFile           File       = {};
            Core::VFS::VFSError  OpenError  = Core::VFS::VFSError::OK;
            uint32_t             Opens      = 0;
            bool                 NullHandle = false;
        };
        Entry    Entries[16] = {};
        uint32_t Count       = 0;
        uint32_t Opens       = 0;

        uint32_t Add(Core::Containers::ArrayView<const uint8_t> bytes, cstring extension)
        {
            EXPECT_LT(Count, 16u);
            EXPECT_LE(bytes.size(), sizeof(Entry::Bytes));
            if (Count >= 16 || bytes.size() > sizeof(Entry::Bytes))
            {
                return UINT32_MAX;
            }
            const uint32_t     index  = Count++;
            auto&              entry  = Entries[index];
            Core::Hash::SHA256 hash   = {};
            Digest256          digest = {};
            EXPECT_TRUE(hash.Update(bytes).Succeeded());
            EXPECT_TRUE(hash.Finalize(&digest).Succeeded());
            EXPECT_TRUE(MakeCookObjectDescriptor(digest, bytes.size(), extension, &entry.Object).Succeeded());
            if (bytes.size())
            {
                (void) Helpers::secure_memcpy(entry.Bytes, sizeof(entry.Bytes), bytes.data(), bytes.size());
            }
            entry.File.Bytes = {entry.Bytes, bytes.size()};
            return index;
        }

        CookObjectReadFns Reader()
        {
            return {
                this,
                [](void* state, const Core::VFS::VFSRelativePath& path) {
                    auto& store = *static_cast<ObjectStore*>(state);
                    ++store.Opens;
                    for (uint32_t i = 0; i < store.Count; ++i)
                    {
                        auto& entry = store.Entries[i];
                        if (entry.Object.Path != path)
                        {
                            continue;
                        }
                        ++entry.Opens;
                        entry.File.Stats = 0;
                        return entry.OpenError != Core::VFS::VFSError::OK ? Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(entry.OpenError) : Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Ok(entry.NullHandle ? nullptr : &entry.File);
                    }
                    return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(Core::VFS::VFSError::NotFound);
                },
                [](void*, Core::VFS::IVFSFile* file) { return file->Close(); }};
        }
    };
} // namespace ZEngine::Cook::Tests
