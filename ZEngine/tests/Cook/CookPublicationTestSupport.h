#pragma once
#include <ZEngine/Cook/CookPublication.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <new>

namespace ZEngine::Cook::Tests
{
    // Faults at a REAL retained root boundary, not live-context overrides.
    // These test-only wrappers use one inline file slot per acquired root.
    struct PublicationFaults
    {
        // Explicitly opt in when exercising budgeted public runs. The default
        // retains this probe's unsupported-accounting failure seam.
        Core::VFS::VFSError MemoryBounds         = Core::VFS::VFSError::Unsupported;
        Core::VFS::VFSError OpenManifest         = Core::VFS::VFSError::OK;
        Core::VFS::VFSError OpenWriter           = Core::VFS::VFSError::OK;
        Core::VFS::VFSError Write                = Core::VFS::VFSError::OK;
        Core::VFS::VFSError Read                 = Core::VFS::VFSError::OK;
        Core::VFS::VFSError Stat                 = Core::VFS::VFSError::OK;
        Core::VFS::VFSError Flush                = Core::VFS::VFSError::OK;
        Core::VFS::VFSError CloseWriter          = Core::VFS::VFSError::OK;
        Core::VFS::VFSError CloseReader          = Core::VFS::VFSError::OK;
        Core::VFS::VFSError RenameObject         = Core::VFS::VFSError::OK;
        Core::VFS::VFSError RenameManifest       = Core::VFS::VFSError::OK;
        Core::VFS::VFSError RemoveTemporary      = Core::VFS::VFSError::OK;
        Core::VFS::VFSError RemoveLock           = Core::VFS::VFSError::OK;
        uint32_t            WriterOpens          = 0;
        uint32_t            ReaderOpens          = 0;
        uint32_t            WriterCloses         = 0;
        uint32_t            ReaderCloses         = 0;
        uint32_t            Writes               = 0;
        uint32_t            Reads                = 0;
        uint32_t            ObjectRenames        = 0;
        uint32_t            ManifestRenames      = 0;
        uint32_t            LockRemovals         = 0;
        uint32_t            TemporaryRemovals    = 0;
        uint32_t            WritesAfterCommit    = 0;
        uint32_t            LiveFiles            = 0;
        size_t              MaxWriteChunk        = SIZE_MAX;
        size_t              MaxReadChunk         = SIZE_MAX;
        bool                ReadStagingOnly      = false;
        bool                CorruptWrites        = false;
        bool                CorruptInstalled     = false;
        bool                Committed            = false;
        // Test-only abrupt-process probes around the actual manifest rename.
        void (*ManifestBoundary)(bool committed) = nullptr;
        // Optional real-filesystem stimulus, not an injected VFS error. Runs
        // once before the first private .tmp write, after captures are ready.
        void (*BeforeStagingWrite)(void*)        = nullptr;
        void* StagingWriteContext                = nullptr;
        bool  BeforeManifestWrite                = false;
    };

    struct PublicationFile final : Core::VFS::IVFSFile
    {
        Core::VFS::IVFSFile*         Inner           = nullptr;
        PublicationFaults*           Faults          = nullptr;
        bool                         Writer          = false;
        bool                         Staging         = false;
        bool                         ManifestStaging = false;

        Core::VFS::VFSResult<size_t> Read(Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) override
        {
            ++Faults->Reads;
            if ((!Faults->ReadStagingOnly || Staging) && Faults->Read != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<size_t>::Fail(Faults->Read);
            return Inner->Read(bytes.first(std::min(bytes.size(), Faults->MaxReadChunk)), offset);
        }
        Core::VFS::VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t> bytes, uint64_t offset) override
        {
            if (Staging && Faults->BeforeStagingWrite && ManifestStaging == Faults->BeforeManifestWrite)
            {
                const auto callback        = Faults->BeforeStagingWrite;
                Faults->BeforeStagingWrite = nullptr;
                callback(Faults->StagingWriteContext);
            }
            ++Faults->Writes;
            Faults->WritesAfterCommit += Faults->Committed ? 1 : 0;
            if (Faults->Write != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<size_t>::Fail(Faults->Write);
            const size_t count = std::min(bytes.size(), Faults->MaxWriteChunk);
            if (Faults->CorruptWrites && count)
            {
                const uint8_t changed = bytes[0] ^ 1;
                return Inner->Write({&changed, 1}, offset);
            }
            return Inner->Write(bytes.first(count), offset);
        }
        Core::VFS::VFSResult<uint64_t> Size() const override
        {
            return Inner->Size();
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> Identity() const override
        {
            return Inner->Identity();
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileStat> Stat() const override
        {
            return (!Faults->ReadStagingOnly || Staging) && Faults->Stat != Core::VFS::VFSError::OK ? Core::VFS::VFSResult<Core::VFS::VFSFileStat>::Fail(Faults->Stat) : Inner->Stat();
        }
        Core::VFS::VFSResult<void> Flush() override
        {
            return Faults->Flush == Core::VFS::VFSError::OK ? Inner->Flush() : Core::VFS::VFSResult<void>::Fail(Faults->Flush);
        }
        const Core::VFS::VFSPath& Path() const override
        {
            return Inner->Path();
        }
        Core::VFS::VFSResult<void> Close() override
        {
            return Inner->Close();
        }
    };

    struct PublicationRoot final : Core::VFS::IVFSRootOperations
    {
        Helpers::Scope<Core::VFS::IVFSRootOperations>                              Inner         = {};
        PublicationFaults*                                                         Faults        = nullptr;
        PublicationFile                                                            File          = {};
        uint64_t                                                                   ProviderBytes = 0;

        static Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>> Wrap(Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>> acquired, PublicationFaults* faults, uint64_t provider_bytes)
        {
            if (acquired.Failed())
                return acquired;
            auto root = Helpers::Scope<PublicationRoot>(new (std::nothrow) PublicationRoot());
            if (!root)
                return Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>>::Fail(Core::VFS::VFSError::OutOfMemory);
            root->Inner         = std::move(acquired.Value());
            root->Faults        = faults;
            root->ProviderBytes = provider_bytes;
            return Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>>::Ok(std::move(root));
        }

        Core::VFS::VFSResult<Core::VFS::VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            if (Faults->MemoryBounds != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<Core::VFS::VFSRootMemoryRequirements>::Fail(Faults->MemoryBounds);
            auto requirements = Inner->MemoryRequirements();
            if (requirements.Succeeded())
                requirements.Value().StateBytes += sizeof(*this) + ProviderBytes;
            return requirements;
        }

        Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>> AcquireChild(const Core::VFS::VFSRelativePath& path) override
        {
            return Wrap(Inner->AcquireChild(path), Faults, ProviderBytes);
        }

        Core::VFS::VFSResult<bool> Overlaps(const Core::VFS::IVFSRootOperations& other) const override
        {
            const auto* wrapped = dynamic_cast<const PublicationRoot*>(&other);
            return Inner->Overlaps(wrapped ? *wrapped->Inner : other);
        }

        Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> Identity(const Core::VFS::VFSRelativePath& path) const override
        {
            return Inner->Identity(path);
        }

        Core::VFS::VFSResult<void> Visit(const Core::VFS::VFSRelativePath* path, uint64_t maximum, Core::Containers::ArrayView<uint8_t> buffer, Core::VFS::VFSRootVisitorFn visitor, void* context) const override
        {
            return Inner->Visit(path, maximum, buffer, visitor, context);
        }

        Core::VFS::VFSPublicationCapabilities Capabilities() const override
        {
            return Inner->Capabilities();
        }
        Core::VFS::VFSResult<Core::VFS::IVFSFile*> Open(const Core::VFS::VFSRelativePath& path, Core::VFS::VFSOpenFlags flags) override
        {
            if (File.Inner)
                return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(Core::VFS::VFSError::OutOfMemory);
            const bool writer = Core::VFS::HasFlag(flags, Core::VFS::VFSOpenFlags::Write);
            const auto error  = writer ? Faults->OpenWriter : path.View() == CookManifestName ? Faults->OpenManifest : Core::VFS::VFSError::OK;
            if (error != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Fail(error);
            const auto opened = Inner->Open(path, flags);
            if (opened.Failed())
                return opened;
            File.Inner           = opened.Value();
            File.Faults          = Faults;
            File.Writer          = writer;
            const auto name      = path.View();
            File.Staging         = name.size() >= 4 && Core::Containers::StringView(name.data() + name.size() - 4, 4) == ".tmp";
            File.ManifestStaging = File.Staging && path.ComponentCount() == 1;
            ++Faults->LiveFiles;
            if (writer)
            {
                ++Faults->WriterOpens;
                EXPECT_TRUE(Core::VFS::HasFlag(flags, Core::VFS::VFSOpenFlags::Exclusive));
                EXPECT_FALSE(Core::VFS::HasFlag(flags, Core::VFS::VFSOpenFlags::Truncate));
            }
            else
                ++Faults->ReaderOpens;
            return Core::VFS::VFSResult<Core::VFS::IVFSFile*>::Ok(&File);
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileStat> Stat(const Core::VFS::VFSRelativePath& path) const override
        {
            return Inner->Stat(path);
        }
        Core::VFS::VFSResult<void> CreateDir(const Core::VFS::VFSRelativePath& path, bool exclusive) override
        {
            return Inner->CreateDir(path, exclusive);
        }
        Core::VFS::VFSResult<void> Remove(const Core::VFS::VFSRelativePath& path) override
        {
            const bool lock = path.View() == CookWriterLockName;
            if (lock)
                ++Faults->LockRemovals;
            else
                ++Faults->TemporaryRemovals;
            const auto error = lock ? Faults->RemoveLock : Faults->RemoveTemporary;
            return error == Core::VFS::VFSError::OK ? Inner->Remove(path) : Core::VFS::VFSResult<void>::Fail(error);
        }
        Core::VFS::VFSResult<void> Rename(const Core::VFS::VFSRelativePath& from, const Core::VFS::VFSRelativePath& to, bool replace) override
        {
            if (replace)
            {
                EXPECT_EQ(to.View(), CookManifestName);
                ++Faults->ManifestRenames;
            }
            else
                ++Faults->ObjectRenames;
            const auto error = replace ? Faults->RenameManifest : Faults->RenameObject;
            if (error != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<void>::Fail(error);
            if (replace && Faults->ManifestBoundary)
                Faults->ManifestBoundary(false);
            const auto result = Inner->Rename(from, to, replace);
            if (!replace && result.Succeeded() && Faults->CorruptInstalled)
            {
                // Fault only after the sealed stage has been verified and
                // renamed: installed bytes require independent verification.
                const auto file = Inner->Open(to, Core::VFS::VFSOpenFlags::Write);
                EXPECT_TRUE(file.Succeeded());
                if (file.Succeeded())
                {
                    constexpr uint8_t changed = 0;
                    EXPECT_EQ(file.Value()->Write({&changed, 1}, 0).Value(), 1u);
                    EXPECT_TRUE(Inner->CloseChecked(file.Value()).Succeeded());
                }
            }
            if (replace && result.Succeeded())
            {
                Faults->Committed = true;
                if (Faults->ManifestBoundary)
                    Faults->ManifestBoundary(true);
            }
            return result;
        }
        Core::VFS::VFSResult<void> CloseChecked(Core::VFS::IVFSFile* file) override
        {
            EXPECT_EQ(file, &File);
            EXPECT_NE(File.Inner, nullptr);
            const auto result = Inner->CloseChecked(File.Inner);
            File.Inner        = nullptr;
            --Faults->LiveFiles;
            if (File.Writer)
                ++Faults->WriterCloses;
            else
                ++Faults->ReaderCloses;
            const auto error = File.Writer ? Faults->CloseWriter : (!Faults->ReadStagingOnly || File.Staging) ? Faults->CloseReader : Core::VFS::VFSError::OK;
            return error == Core::VFS::VFSError::OK ? result : Core::VFS::VFSResult<void>::Fail(error);
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileIdentity> RootIdentity() const override
        {
            return Inner->RootIdentity();
        }
    };

    class PublicationProvider final : public Core::VFS::IVFSRootProvider
    {
    public:
        Helpers::Ref<Core::VFS::IVFSRootProvider>                  Inner  = {};
        PublicationFaults*                                         Faults = nullptr;

        Core::VFS::VFSResult<Core::VFS::VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            if (Faults->MemoryBounds != Core::VFS::VFSError::OK)
                return Core::VFS::VFSResult<Core::VFS::VFSRootMemoryRequirements>::Fail(Faults->MemoryBounds);
            auto requirements = Inner->MemoryRequirements();
            if (requirements.Succeeded())
                requirements.Value().StateBytes += sizeof(PublicationRoot) + sizeof(*this);
            return requirements;
        }

    protected:
        Core::VFS::VFSResult<Helpers::Scope<Core::VFS::IVFSRootOperations>> AcquireRoot(const Core::VFS::VFSPath& path, Core::VFS::VFSRootAccess access) override
        {
            return PublicationRoot::Wrap(Inner->Acquire(path, access), Faults, sizeof(*this));
        }
    };

    struct PublicationBackend final : Core::VFS::IVFSBackend
    {
        Core::VFS::IVFSBackend*                   Inner    = nullptr;
        Helpers::Ref<PublicationProvider>         Provider = {};
        Helpers::Ref<Core::VFS::IVFSRootProvider> RootProvider() const override
        {
            return Provider;
        }
        Core::VFS::VFSResult<Core::VFS::IVFSFile*> Open(const Core::VFS::VFSPath& path, Core::VFS::VFSOpenFlags flags) override
        {
            return Inner->Open(path, flags);
        }
        void Close(Core::VFS::IVFSFile* file) override
        {
            Inner->Close(file);
        }
        Core::VFS::VFSResult<Core::VFS::VFSFileStat> Stat(const Core::VFS::VFSPath& path) const override
        {
            return Inner->Stat(path);
        }
        bool Exists(const Core::VFS::VFSPath& path) const override
        {
            return Inner->Exists(path);
        }
        Core::VFS::VFSResult<Core::Containers::Array<Core::VFS::VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const Core::VFS::VFSPath& path) const override
        {
            return Inner->List(arena, path);
        }
        Core::VFS::VFSResult<void> CreateDir(const Core::VFS::VFSPath& path) override
        {
            return Inner->CreateDir(path);
        }
        Core::VFS::VFSResult<void> CreateDirExclusive(const Core::VFS::VFSPath& path) override
        {
            return Inner->CreateDirExclusive(path);
        }
        Core::VFS::VFSResult<void> Remove(const Core::VFS::VFSPath& path) override
        {
            return Inner->Remove(path);
        }
        Core::VFS::VFSResult<void> Rename(const Core::VFS::VFSPath& from, const Core::VFS::VFSPath& to) override
        {
            return Inner->Rename(from, to);
        }
        Core::VFS::VFSResult<void> RenameNoReplace(const Core::VFS::VFSPath& from, const Core::VFS::VFSPath& to) override
        {
            return Inner->RenameNoReplace(from, to);
        }
        cstring BackendType() const override
        {
            return "PublicationFaults";
        }
        Core::VFS::VFSBackendCaps Capabilities() const override
        {
            return Inner->Capabilities();
        }
    };
} // namespace ZEngine::Cook::Tests
