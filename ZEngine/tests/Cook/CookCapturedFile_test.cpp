#include <ZEngine/Cook/CookCapturedFile.h>
#include <ZEngine/Cook/CookInputLease.h>
#include <ZEngine/Cook/CookLibraryExclusion.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <type_traits>
#include "../TemporaryDirectory.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath CapturePath(cstring text)
        {
            return VFS::VFSPath::Parse(text).Value();
        }

        VFS::VFSRelativePath CaptureRelative(cstring text)
        {
            return VFS::VFSRelativePath::Parse(text).Value();
        }

        struct CaptureSource final : VFS::IVFSFile
        {
            uint8_t                Bytes[1024]   = {'a', 'b', 'c'};
            uint64_t               Length        = 3;
            uint64_t               StatLength    = 3;
            uint64_t               SizeLength    = 3;
            uint64_t               FinalLength   = 3;
            uint64_t               StopAt        = std::numeric_limits<uint64_t>::max();
            uint64_t               FailAt        = std::numeric_limits<uint64_t>::max();
            size_t                 MaxChunk      = SIZE_MAX;
            VFS::VFSError          StatError     = VFS::VFSError::OK;
            VFS::VFSError          SizeError     = VFS::VFSError::OK;
            VFS::VFSPath           FilePath      = {};
            bool                   ChangeSize    = false;
            bool                   Directory     = false;
            bool                   OversizedRead = false;
            mutable uint32_t       SizeCalls     = 0;
            mutable uint32_t       StatCalls     = 0;
            uint32_t               Reads         = 0;
            uint32_t               Closes        = 0;

            VFS::VFSResult<size_t> Read(Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) override
            {
                ++Reads;
                if (offset >= FailAt)
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
                if (OversizedRead)
                    return VFS::VFSResult<size_t>::Ok(bytes.size() + 1);
                const auto available = std::min(Length, StopAt);
                if (offset >= available)
                    return VFS::VFSResult<size_t>::Ok(0);
                const auto count = static_cast<size_t>(std::min<uint64_t>(std::min(bytes.size(), MaxChunk), available - offset));
                if (count)
                    EXPECT_EQ(Helpers::secure_memcpy(bytes.data(), bytes.size(), Bytes + offset, count), Helpers::MEMORY_OP_SUCCESS);
                return VFS::VFSResult<size_t>::Ok(count);
            }

            VFS::VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t>, uint64_t) override
            {
                return VFS::VFSResult<size_t>::Fail(VFS::VFSError::PermissionDenied);
            }
            VFS::VFSResult<uint64_t> Size() const override
            {
                ++SizeCalls;
                return SizeError != VFS::VFSError::OK ? VFS::VFSResult<uint64_t>::Fail(SizeError) : VFS::VFSResult<uint64_t>::Ok(ChangeSize && SizeCalls > 1 ? FinalLength : SizeLength);
            }
            VFS::VFSResult<VFS::VFSFileStat> Stat() const override
            {
                ++StatCalls;
                return StatError != VFS::VFSError::OK ? VFS::VFSResult<VFS::VFSFileStat>::Fail(StatError) : VFS::VFSResult<VFS::VFSFileStat>::Ok({.SizeBytes = StatLength, .IsDirectory = Directory});
            }
            VFS::VFSResult<void> Flush() override
            {
                return VFS::VFSResult<void>::Ok();
            }
            const VFS::VFSPath& Path() const override
            {
                return FilePath;
            }
            VFS::VFSResult<void> Close() override
            {
                ++Closes;
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct CaptureFixedAllocator final : ICookAllocator
        {
            ICookAllocator*       Parent       = nullptr;
            uint64_t              FixedBytes   = 0;
            uint32_t              FixedCalls   = 0;
            VFS::VFSError         FixedFailure = VFS::VFSError::OK;

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++FixedCalls;
                FixedBytes = bytes;
                return FixedFailure == VFS::VFSError::OK ? Parent->TryCharge(bytes, category, output) : VFS::VFSResult<void>::Fail(FixedFailure);
            }
        };

        struct RangeCapture final : ICookCapturedFile
        {
            mutable CaptureSource Source = {};
            uint64_t              Size() const noexcept override
            {
                return Source.SizeLength;
            }
            Digest256 Digest() const noexcept override
            {
                return {};
            }
            VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> bytes) const override
            {
                return Source.Read(bytes, offset);
            }
        };

        struct CaptureFaults
        {
            size_t        MaxWriteChunk  = SIZE_MAX;
            VFS::VFSError WriteError     = VFS::VFSError::OK;
            VFS::VFSError ReadError      = VFS::VFSError::OK;
            VFS::VFSError FlushError     = VFS::VFSError::OK;
            VFS::VFSError CloseError     = VFS::VFSError::OK;
            VFS::VFSError RemoveError    = VFS::VFSError::OK;
            bool          OversizedWrite = false;
            uint32_t      Writes         = 0;
            uint32_t      Flushes        = 0;
            uint32_t      WriterCloses   = 0;
            uint32_t      ReaderCloses   = 0;
        };

        // A real rooted memory backend with faults only at the retained I/O
        // boundary. Live-context overrides must not intercept capture operations.
        struct CaptureFaultFile final : VFS::IVFSFile
        {
            VFS::IVFSFile*         File   = nullptr;
            CaptureFaults*         Faults = nullptr;
            bool                   Writer = false;

            VFS::VFSResult<size_t> Read(Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) override
            {
                return Faults->ReadError != VFS::VFSError::OK ? VFS::VFSResult<size_t>::Fail(Faults->ReadError) : File->Read(bytes, offset);
            }
            VFS::VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t> bytes, uint64_t offset) override
            {
                ++Faults->Writes;
                if (Faults->WriteError != VFS::VFSError::OK)
                    return VFS::VFSResult<size_t>::Fail(Faults->WriteError);
                if (Faults->OversizedWrite)
                    return VFS::VFSResult<size_t>::Ok(bytes.size() + 1);
                return File->Write({bytes.data(), std::min(bytes.size(), Faults->MaxWriteChunk)}, offset);
            }
            VFS::VFSResult<uint64_t> Size() const override
            {
                return File->Size();
            }
            VFS::VFSResult<VFS::VFSFileStat> Stat() const override
            {
                return File->Stat();
            }
            VFS::VFSResult<void> Flush() override
            {
                ++Faults->Flushes;
                return Faults->FlushError != VFS::VFSError::OK ? VFS::VFSResult<void>::Fail(Faults->FlushError) : File->Flush();
            }
            const VFS::VFSPath& Path() const override
            {
                return File->Path();
            }
            VFS::VFSResult<void> Close() override
            {
                return File->Close();
            }
        };

        struct CaptureFaultRoot final : VFS::IVFSRootOperations
        {
            Helpers::Scope<VFS::IVFSRootOperations> Inner  = {};
            CaptureFaults*                          Faults = nullptr;
            CaptureFaultFile                        File   = {};

            VFS::VFSPublicationCapabilities         Capabilities() const override
            {
                return Inner->Capabilities();
            }
            VFS::VFSResult<VFS::IVFSFile*> Open(const VFS::VFSRelativePath& path, VFS::VFSOpenFlags flags) override
            {
                if (File.File)
                    return VFS::VFSResult<VFS::IVFSFile*>::Fail(VFS::VFSError::OutOfMemory);
                const auto opened = Inner->Open(path, flags);
                if (opened.Failed())
                    return opened;
                File.File   = opened.Value();
                File.Faults = Faults;
                File.Writer = VFS::HasFlag(flags, VFS::VFSOpenFlags::Write);
                if (File.Writer)
                {
                    EXPECT_TRUE(VFS::HasFlag(flags, VFS::VFSOpenFlags::Exclusive));
                    EXPECT_FALSE(VFS::HasFlag(flags, VFS::VFSOpenFlags::Truncate));
                }
                return VFS::VFSResult<VFS::IVFSFile*>::Ok(&File);
            }
            VFS::VFSResult<VFS::VFSFileStat> Stat(const VFS::VFSRelativePath& path) const override
            {
                return Inner->Stat(path);
            }
            VFS::VFSResult<void> CreateDir(const VFS::VFSRelativePath& path, bool exclusive) override
            {
                return Inner->CreateDir(path, exclusive);
            }
            VFS::VFSResult<void> Remove(const VFS::VFSRelativePath& path) override
            {
                return Faults->RemoveError != VFS::VFSError::OK ? VFS::VFSResult<void>::Fail(Faults->RemoveError) : Inner->Remove(path);
            }
            VFS::VFSResult<void> Rename(const VFS::VFSRelativePath& from, const VFS::VFSRelativePath& to, bool replace) override
            {
                return Inner->Rename(from, to, replace);
            }
            VFS::VFSResult<void> CloseChecked(VFS::IVFSFile* file) override
            {
                if (!file || file != &File || !File.File)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
                if (File.Writer)
                    ++Faults->WriterCloses;
                else
                    ++Faults->ReaderCloses;
                const auto close = Inner->CloseChecked(File.File);
                File.File        = nullptr;
                return Faults->CloseError != VFS::VFSError::OK ? VFS::VFSResult<void>::Fail(Faults->CloseError) : close;
            }
        };

        class CaptureFaultProvider final : public VFS::IVFSRootProvider
        {
        public:
            Helpers::Ref<VFS::IVFSRootProvider> Inner  = {};
            CaptureFaults*                      Faults = nullptr;

        protected:
            VFS::VFSResult<Helpers::Scope<VFS::IVFSRootOperations>> AcquireRoot(const VFS::VFSPath& path, VFS::VFSRootAccess access) override
            {
                auto acquired = Inner->Acquire(path, access);
                if (acquired.Failed())
                    return acquired;
                auto root = Helpers::Scope<CaptureFaultRoot>(new (std::nothrow) CaptureFaultRoot());
                if (!root)
                    return VFS::VFSResult<Helpers::Scope<VFS::IVFSRootOperations>>::Fail(VFS::VFSError::OutOfMemory);
                root->Inner  = std::move(acquired.Value());
                root->Faults = Faults;
                return VFS::VFSResult<Helpers::Scope<VFS::IVFSRootOperations>>::Ok(std::move(root));
            }
        };

        struct CaptureFaultBackend final : VFS::IVFSBackend
        {
            Helpers::Ref<CaptureFaultProvider>  Provider = {};
            VFS::VFSMemoryBackend*              Backend  = nullptr;
            Helpers::Ref<VFS::IVFSRootProvider> RootProvider() const override
            {
                return Provider;
            }
            VFS::VFSResult<VFS::IVFSFile*> Open(const VFS::VFSPath& path, VFS::VFSOpenFlags flags) override
            {
                return Backend->Open(path, flags);
            }
            void Close(VFS::IVFSFile* file) override
            {
                Backend->Close(file);
            }
            VFS::VFSResult<VFS::VFSFileStat> Stat(const VFS::VFSPath& path) const override
            {
                return Backend->Stat(path);
            }
            bool Exists(const VFS::VFSPath& path) const override
            {
                return Backend->Exists(path);
            }
            VFS::VFSResult<Core::Containers::Array<VFS::VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFS::VFSPath& path) const override
            {
                return Backend->List(arena, path);
            }
            cstring BackendType() const override
            {
                return "CaptureFaults";
            }
            VFS::VFSBackendCaps Capabilities() const override
            {
                return Backend->Capabilities();
            }
        };

        void ExpectCapturedABC(const CookCapturedFile& capture)
        {
            ASSERT_TRUE(capture.IsValid());
            EXPECT_EQ(capture.Size(), 3u);
            const auto hex = capture.Digest().Hex();
            EXPECT_STREQ(hex.data(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
            uint8_t    bytes[8] = {};
            const auto read     = capture.Read(0, bytes);
            ASSERT_TRUE(read.Succeeded());
            ASSERT_EQ(read.Value(), 3u);
            EXPECT_EQ(bytes[0], 'a');
            EXPECT_EQ(bytes[1], 'b');
            EXPECT_EQ(bytes[2], 'c');
        }
    } // namespace

    class CookCapturedFileTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager         Memory    = {};
        VFS::VFSMemoryBackend               Backend   = {};
        VFS::VFSContext                     Context   = {};
        VFS::VFSScratchRootLease            Scratch   = {};
        Tests::FixedAllocator<ZKilo(16), 8> Allocator = {};
        CookWorkspace                       Stream    = {};
        CookLimits                          Limits    = {.MaxWorkingBytes = ZKilo(128)};
        CookCaptureStore                    Store     = {};
        CaptureSource                       Source    = {};

        void                                SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(2), 128).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(CapturePath("/scratch")).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(CapturePath("/scratch"), &Scratch).Succeeded());
            ASSERT_TRUE(Scratch.CreateDirExclusive(CaptureRelative("run")).Succeeded());
            ASSERT_TRUE(Allocator.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Stream.Initialize(Allocator, 64, CookMemoryCategory::Captures).Succeeded());
            ASSERT_TRUE(Store.Initialize(CaptureContext(), {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        }

        CookCaptureContext CaptureContext()
        {
            return {&Scratch, CaptureRelative("run"), &Allocator, &Limits};
        }

        void ExpectEmptyRun()
        {
            Core::Memory::ArenaScratch scratch(&Memory.MainArena);
            const auto                 list = Backend.List(&Memory.MainArena, CapturePath("/scratch/run"));
            ASSERT_TRUE(list.Succeeded());
            EXPECT_EQ(list.Value().size(), 0u);
        }

        void TearDown() override
        {
            EXPECT_EQ(Store.OutstandingCaptures(), 0u);
            EXPECT_EQ(Store.LiveBytes(), 0u);
            EXPECT_EQ(Store.CleanupError(), VFS::VFSError::OK);
            Stream.Release();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            Scratch.Reset();
            Context.Shutdown();
        }
    };

    TEST(CookCapturedRangeTest, ReadsExactSubrangeAcrossShortTransfersWithoutAllocating)
    {
        RangeCapture capture    = {};
        capture.Source.MaxChunk = 1;
        uint8_t bytes[2]        = {};
        ASSERT_TRUE(ReadCookCapturedRange(capture, 1, bytes).Succeeded());
        EXPECT_EQ(bytes[0], 'b');
        EXPECT_EQ(bytes[1], 'c');
        EXPECT_EQ(capture.Source.Reads, 2u);
    }

    TEST(CookCapturedRangeTest, EmptyAndInvalidRangesNeverReachTheUnderlyingReader)
    {
        RangeCapture capture  = {};
        uint8_t      bytes[4] = {};
        EXPECT_TRUE(ReadCookCapturedRange(capture, 0, {}).Succeeded());
        EXPECT_TRUE(ReadCookCapturedRange(capture, 3, {}).Succeeded());
        EXPECT_EQ(ReadCookCapturedRange(capture, 4, {}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ReadCookCapturedRange(capture, 0, bytes).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ReadCookCapturedRange(capture, UINT64_MAX, {bytes, 1}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ReadCookCapturedRange(capture, 0, {nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(capture.Source.Reads, 0u);
    }

    TEST(CookCapturedRangeTest, PrematureEOFOverReportingAndActualIOFailuresAreRejected)
    {
        uint8_t      bytes[3]   = {};
        RangeCapture capture    = {};
        capture.Source.MaxChunk = 1;
        capture.Source.StopAt   = 2;
        EXPECT_EQ(ReadCookCapturedRange(capture, 0, bytes).Error(), VFS::VFSError::IOError);
        capture.Source.StopAt = UINT64_MAX;
        capture.Source.FailAt = 1;
        EXPECT_EQ(ReadCookCapturedRange(capture, 0, bytes).Error(), VFS::VFSError::PermissionDenied);
        capture.Source.FailAt        = UINT64_MAX;
        capture.Source.OversizedRead = true;
        EXPECT_EQ(ReadCookCapturedRange(capture, 0, bytes).Error(), VFS::VFSError::IOError);
    }

    TEST(CookCapturedFileDefaultsTest, EmptyValuesAreInertAndMoveOnly)
    {
        static_assert(!std::is_copy_constructible_v<CookCapturedFile>);
        static_assert(std::is_nothrow_move_constructible_v<CookCapturedFile>);
        static_assert(std::is_nothrow_move_assignable_v<CookCapturedFile>);
        static_assert(!std::is_move_constructible_v<CookCaptureStore>);
        CookCapturedFile capture = {};
        CookCaptureStore store   = {};
        CaptureSource    source  = {};
        EXPECT_FALSE(capture.IsValid());
        EXPECT_EQ(capture.Size(), 0u);
        EXPECT_EQ(capture.Digest(), Digest256{});
        EXPECT_EQ(capture.Read(0, {}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_TRUE(capture.Reset().Succeeded());
        EXPECT_TRUE(capture.Reset().Succeeded());
        EXPECT_EQ(store.Initialize({}, {}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(store.CaptureMemory(source, 3, &capture).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(store.CaptureDisk(source, 3, &capture).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookCapturedFileTest, PrincipalCaptureStateIsReservedBeforeSourceIOAndReleasedWithoutAnAllocation)
    {
        CaptureFixedAllocator allocator = {};
        allocator.Parent                = &Allocator;
        auto context                    = CaptureContext();
        context.Allocator               = &allocator;
        CookCaptureStore store          = {};
        ASSERT_TRUE(store.Initialize(context, {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        const auto retained = Allocator.Statistics().LiveBytes;
        for (bool disk : {false, true})
        {
            CookCapturedFile output = {};
            ASSERT_TRUE(store.CaptureMemory(Source, 3, &output).Succeeded());
            const uint64_t fixed_bytes = allocator.FixedBytes;
            EXPECT_EQ(fixed_bytes, Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookCapturedFile) + sizeof(uint8_t) + sizeof(CookMemoryCharge));
            for (uint64_t shortfall : {1u, 0u})
            {
                SCOPED_TRACE(disk);
                SCOPED_TRACE(shortfall);
                const auto       before       = Allocator.Statistics().LiveBytes;
                const uint64_t   result_bytes = !shortfall && !disk ? 3 : 0;
                CookMemoryCharge occupied     = {};
                ASSERT_GE(Allocator.Ledger.RemainingBytes(), fixed_bytes + result_bytes);
                ASSERT_TRUE(Allocator.TryCharge(Allocator.Ledger.RemainingBytes() - fixed_bytes - result_bytes + shortfall, CookMemoryCategory::Captures, &occupied).Succeeded());
                Source                 = {};
                const auto allocations = Allocator.Allocations;
                const auto captured    = disk ? store.CaptureDisk(Source, 3, &output) : store.CaptureMemory(Source, 3, &output);
                EXPECT_EQ(captured.Error(), shortfall ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
                if (shortfall)
                {
                    EXPECT_EQ(Source.Reads, 0u);
                    EXPECT_EQ(Source.SizeCalls, 0u);
                    EXPECT_EQ(Source.StatCalls, 0u);
                    EXPECT_EQ(Allocator.Allocations, allocations);
                    EXPECT_EQ(Allocator.Statistics().LiveBytes, before + occupied.Bytes());
                    ExpectEmptyRun();
                }
                else
                {
                    EXPECT_EQ(Allocator.Allocations, allocations + (disk ? 0 : 1));
                    EXPECT_EQ(Allocator.Statistics().LiveBytes, retained + occupied.Bytes() + (disk ? 0 : 3));
                }
                ExpectCapturedABC(output);
                EXPECT_EQ(store.OutstandingCaptures(), 1u);
                EXPECT_EQ(store.LiveBytes(), 3u);
            }
            ASSERT_TRUE(output.Reset().Succeeded());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, retained);
            ExpectEmptyRun();
        }
    }

    TEST_F(CookCapturedFileTest, PrincipalCaptureRefusalPreservesTheExistingOutputWithoutSourceOrScratchIO)
    {
        CookCapturedFile output = {};
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &output).Succeeded());
        const auto            retained    = Allocator.Statistics().LiveBytes;
        const auto            allocations = Allocator.Allocations;
        CaptureFixedAllocator allocator   = {};
        allocator.Parent                  = &Allocator;
        auto context                      = CaptureContext();
        context.Allocator                 = &allocator;
        CookCaptureStore store            = {};
        ASSERT_TRUE(store.Initialize(context, {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        for (auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory, VFS::VFSError::Unsupported})
            for (bool disk : {false, true})
            {
                SCOPED_TRACE(disk);
                allocator.FixedFailure = error;
                Source                 = {};
                EXPECT_EQ((disk ? store.CaptureDisk(Source, 3, &output) : store.CaptureMemory(Source, 3, &output)).Error(), error);
                EXPECT_EQ(Source.Reads, 0u);
                EXPECT_EQ(Source.SizeCalls, 0u);
                EXPECT_EQ(Source.StatCalls, 0u);
                EXPECT_EQ(Allocator.Allocations, allocations);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, retained);
                EXPECT_EQ(store.LiveBytes(), 0u);
                EXPECT_EQ(store.OutstandingCaptures(), 0u);
                ExpectCapturedABC(output);
                ExpectEmptyRun();
            }
    }

    TEST_F(CookCapturedFileTest, PrincipalSealHashBudgetSupportsExactReuseAndRejectsBeforeSourceIO)
    {
        CaptureFixedAllocator allocator = {};
        allocator.Parent                = &Allocator;
        auto context                    = CaptureContext();
        context.Allocator               = &allocator;
        CookCaptureStore store          = {};
        ASSERT_TRUE(store.Initialize(context, {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        const auto digest = Digest256::ParseHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad").Value();
        ASSERT_TRUE(store.VerifySource(Source, 3, digest).Succeeded());
        const uint64_t fixed_bytes = allocator.FixedBytes;
        EXPECT_EQ(fixed_bytes, Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(uint8_t) + sizeof(CookMemoryCharge));
        const auto retained    = Allocator.Statistics().LiveBytes;
        const auto allocations = Allocator.Allocations;
        for (uint64_t shortfall : {1u, 0u})
        {
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(Allocator.TryCharge(Allocator.Ledger.RemainingBytes() - fixed_bytes + shortfall, CookMemoryCategory::Captures, &occupied).Succeeded());
            Source = {};
            EXPECT_EQ(store.VerifySource(Source, 3, digest).Error(), shortfall ? VFS::VFSError::SizeLimitExceeded : VFS::VFSError::OK);
            if (shortfall)
            {
                EXPECT_EQ(Source.Reads, 0u);
                EXPECT_EQ(Source.SizeCalls, 0u);
                EXPECT_EQ(Source.StatCalls, 0u);
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, retained + occupied.Bytes());
        }
        for (auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory})
        {
            allocator.FixedFailure = error;
            Source                 = {};
            EXPECT_EQ(store.VerifySource(Source, 3, digest).Error(), error);
            EXPECT_EQ(Source.Reads, 0u);
            EXPECT_EQ(Source.SizeCalls, 0u);
            EXPECT_EQ(Source.StatCalls, 0u);
        }
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, retained);
        EXPECT_EQ(store.OutstandingCaptures(), 0u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, MemoryCaptureUsesOneAllocationAndOwnsExactBytesAndDigest)
    {
        CookCapturedFile capture = {};
        Source.MaxChunk          = 1;
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &capture).Succeeded());
        EXPECT_EQ(Allocator.Allocations, 2u); // stream + one exact capture block
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 67u);
        EXPECT_EQ(Store.LiveBytes(), 3u);
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Source.Reads, 4u);  // three short reads + EOF check
        EXPECT_EQ(Source.Closes, 0u); // Source ownership stays with the provider.
        Source.Bytes[0] = 0;
        ExpectCapturedABC(capture);
        EXPECT_TRUE(capture.Reset().Succeeded());
        EXPECT_EQ(Allocator.Frees, 1u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, DiskCaptureUsesOnlyThePrechargedStreamAndCleansItsOwnedFile)
    {
        CookCapturedFile capture = {};
        Source.MaxChunk          = 1;
        ASSERT_TRUE(Store.CaptureDisk(Source, 3, &capture).Succeeded());
        EXPECT_EQ(Allocator.Allocations, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 64u);
        EXPECT_EQ(Store.LiveBytes(), 3u);
        Source.Bytes[0] = 0;
        ExpectCapturedABC(capture);
        EXPECT_TRUE(Scratch.Exists(CaptureRelative("run/capture-0000000000000000")).Value());
        ASSERT_TRUE(capture.Reset().Succeeded());
        EXPECT_FALSE(Scratch.Exists(CaptureRelative("run/capture-0000000000000000")).Value());
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, ReadsClampToCapturedBoundsAndRejectNullStorage)
    {
        for (bool disk : {false, true})
        {
            CookCapturedFile capture = {};
            ASSERT_TRUE((disk ? Store.CaptureDisk(Source, 3, &capture) : Store.CaptureMemory(Source, 3, &capture)).Succeeded());
            uint8_t    bytes[4] = {9, 9, 9, 9};
            const auto read     = capture.Read(1, bytes);
            ASSERT_TRUE(read.Succeeded());
            EXPECT_EQ(read.Value(), 2u);
            EXPECT_EQ(bytes[0], 'b');
            EXPECT_EQ(bytes[1], 'c');
            EXPECT_EQ(bytes[2], 9);
            EXPECT_EQ(capture.Read(3, bytes).Value(), 0u);
            EXPECT_EQ(capture.Read(UINT64_MAX, bytes).Value(), 0u);
            EXPECT_EQ(capture.Read(0, {}).Value(), 0u);
            EXPECT_EQ(capture.Read(0, {nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        }
    }

    TEST_F(CookCapturedFileTest, EmptyCapturesNeedNeitherMemoryAllocationNorStreamStorage)
    {
        Source.Length = Source.StatLength = Source.SizeLength = 0;
        CookCaptureStore store                                = {};
        ASSERT_TRUE(store.Initialize(CaptureContext(), {}).Succeeded());
        for (bool disk : {false, true})
        {
            CookCapturedFile capture = {};
            ASSERT_TRUE((disk ? store.CaptureDisk(Source, 0, &capture) : store.CaptureMemory(Source, 0, &capture)).Succeeded());
            EXPECT_TRUE(capture.IsValid());
            EXPECT_EQ(capture.Size(), 0u);
            const auto hex = capture.Digest().Hex();
            EXPECT_STREQ(hex.data(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
            EXPECT_EQ(Allocator.Allocations, 1u);
            EXPECT_EQ(store.LiveBytes(), 0u);
            EXPECT_EQ(store.OutstandingCaptures(), 1u);
        }
        EXPECT_EQ(store.OutstandingCaptures(), 0u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, PerFileAndAggregateBudgetsPreserveAnExistingOutput)
    {
        Limits.MaxCapturedBytes = 5;
        CookCaptureStore store  = {};
        ASSERT_TRUE(store.Initialize(CaptureContext(), {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        CookCapturedFile capture = {};
        EXPECT_EQ(store.CaptureMemory(Source, 2, &capture).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Allocator.Allocations, 1u);
        ASSERT_TRUE(store.CaptureMemory(Source, 3, &capture).Succeeded());
        EXPECT_EQ(store.CaptureMemory(Source, 3, &capture).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(store.CaptureDisk(Source, 3, &capture).Error(), VFS::VFSError::SizeLimitExceeded);
        ExpectCapturedABC(capture);
        EXPECT_EQ(store.LiveBytes(), 3u);
        EXPECT_EQ(store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Allocator.Allocations, 2u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, AllocationRefusalPreservesOutputAndAccounting)
    {
        CookCapturedFile capture = {};
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &capture).Succeeded());
        for (auto error : {VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            Allocator.Failure = error;
            EXPECT_EQ(Store.CaptureMemory(Source, 3, &capture).Error(), error);
            ExpectCapturedABC(capture);
            EXPECT_EQ(Store.LiveBytes(), 3u);
            EXPECT_EQ(Store.OutstandingCaptures(), 1u);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 67u);
        }
    }

    TEST_F(CookCapturedFileTest, InvalidSourceTypeSizeAndMetadataFailBeforeAllocating)
    {
        CookCapturedFile capture = {};
        Source.Directory         = true;
        EXPECT_EQ(Store.CaptureMemory(Source, 3, &capture).Error(), VFS::VFSError::NotAFile);
        Source.Directory  = false;
        Source.StatLength = 4;
        EXPECT_EQ(Store.CaptureDisk(Source, 3, &capture).Error(), VFS::VFSError::Corrupted);
        Source.StatError = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(Store.CaptureMemory(Source, 3, &capture).Error(), VFS::VFSError::PermissionDenied);
        Source.StatError = VFS::VFSError::OK;
        Source.SizeError = VFS::VFSError::IOError;
        EXPECT_EQ(Store.CaptureMemory(Source, 3, &capture).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Allocator.Allocations, 1u);
        EXPECT_EQ(Store.OutstandingCaptures(), 0u);
        EXPECT_EQ(Store.LiveBytes(), 0u);
        EXPECT_EQ(Source.Reads, 0u);
        EXPECT_FALSE(capture.IsValid());
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, ShortZeroOversizedAndFailedReadsReleasePendingStorage)
    {
        CookCapturedFile capture = {};
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &capture).Succeeded());
        for (bool disk : {false, true})
        {
            for (uint32_t fault = 0; fault < 4; ++fault)
            {
                CaptureSource source = {};
                source.MaxChunk      = 1;
                if (fault == 0)
                    source.StopAt = 2;
                if (fault == 1)
                    source.MaxChunk = 0;
                if (fault == 2)
                    source.OversizedRead = true;
                if (fault == 3)
                    source.FailAt = 1;
                const auto result = disk ? Store.CaptureDisk(source, 3, &capture) : Store.CaptureMemory(source, 3, &capture);
                EXPECT_EQ(result.Error(), fault == 3 ? VFS::VFSError::PermissionDenied : VFS::VFSError::IOError);
                ExpectCapturedABC(capture);
                EXPECT_EQ(Store.LiveBytes(), 3u);
                EXPECT_EQ(Store.OutstandingCaptures(), 1u);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, 67u);
                ExpectEmptyRun();
            }
        }
    }

    TEST_F(CookCapturedFileTest, EndGrowthSizeChangesAndEOFReadErrorsAreRejected)
    {
        for (bool disk : {false, true})
        {
            for (uint32_t fault = 0; fault < 3; ++fault)
            {
                CaptureSource source = {};
                if (fault == 0)
                    source.Length = 4;
                if (fault == 1)
                {
                    source.ChangeSize  = true;
                    source.FinalLength = 2;
                }
                if (fault == 2)
                    source.FailAt = 3;
                CookCapturedFile capture = {};
                const auto       result  = disk ? Store.CaptureDisk(source, 3, &capture) : Store.CaptureMemory(source, 3, &capture);
                EXPECT_EQ(result.Error(), fault == 2 ? VFS::VFSError::PermissionDenied : VFS::VFSError::Corrupted);
                EXPECT_FALSE(capture.IsValid());
                EXPECT_EQ(Store.LiveBytes(), 0u);
                EXPECT_EQ(Store.OutstandingCaptures(), 0u);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, 64u);
                ExpectEmptyRun();
            }
        }
    }

    TEST_F(CookCapturedFileTest, ExclusiveNameCollisionDoesNotRemoveOrTruncateAnUnownedFile)
    {
        constexpr uint8_t sentinel[] = {9, 8, 7, 6};
        ASSERT_TRUE(Backend.WriteFile(CapturePath("/scratch/run/capture-0000000000000000"), sentinel).Succeeded());
        {
            CookCapturedFile capture = {};
            ASSERT_TRUE(Store.CaptureDisk(Source, 3, &capture).Succeeded());
            ExpectCapturedABC(capture);
            EXPECT_TRUE(Scratch.Exists(CaptureRelative("run/capture-0000000000000001")).Value());
        }
        EXPECT_FALSE(Scratch.Exists(CaptureRelative("run/capture-0000000000000001")).Value());
        const auto file = Scratch.Open(CaptureRelative("run/capture-0000000000000000"), VFS::VFSOpenFlags::Read);
        ASSERT_TRUE(file.Succeeded());
        uint8_t    bytes[4] = {};
        const auto read     = file.Value()->Read(bytes, 0);
        const auto close    = Scratch.CloseChecked(file.Value());
        ASSERT_TRUE(read.Succeeded());
        ASSERT_TRUE(close.Succeeded());
        EXPECT_EQ(read.Value(), sizeof(sentinel));
        EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), sentinel, sizeof(sentinel), sizeof(sentinel)), 0);
    }

    TEST_F(CookCapturedFileTest, ManyDiskCapturesDoNotRetainFileWrappers)
    {
        {
            Core::Containers::FixedArray<CookCapturedFile, 64> captures = {};
            for (auto& capture : captures)
                ASSERT_TRUE(Store.CaptureDisk(Source, 3, &capture).Succeeded());
            EXPECT_EQ(Store.LiveBytes(), 192u);
            EXPECT_EQ(Store.OutstandingCaptures(), 64u);
            EXPECT_EQ(Allocator.Allocations, 1u);
            for (const auto& capture : captures)
                ExpectCapturedABC(capture);
        }
        EXPECT_EQ(Store.OutstandingCaptures(), 0u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, MovesAndReplacementTransferExactlyOneOwner)
    {
        CookCapturedFile first = {}, second = {};
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &first).Succeeded());
        ASSERT_TRUE(Store.CaptureDisk(Source, 3, &second).Succeeded());
        CookCapturedFile moved(std::move(first));
        EXPECT_FALSE(first.IsValid());
        EXPECT_EQ(first.Size(), 0u);
        EXPECT_EQ(first.Digest(), Digest256{});
        second = std::move(moved);
        EXPECT_FALSE(moved.IsValid());
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Store.LiveBytes(), 3u);
        auto* same = &second;
        second     = std::move(*same);
        ExpectCapturedABC(second);
        ExpectEmptyRun();
        ASSERT_TRUE(Store.CaptureDisk(Source, 3, &second).Succeeded());
        EXPECT_EQ(Allocator.Frees, 1u);
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        ExpectCapturedABC(second);
        EXPECT_TRUE(second.Reset().Succeeded());
        EXPECT_TRUE(second.Reset().Succeeded());
    }

    TEST_F(CookCapturedFileTest, CapturesRemainReadableAfterContextTeardownAndBackendReplacement)
    {
        CookCapturedFile capture = {};
        {
            CaptureSource source = {};
            ASSERT_TRUE(Store.CaptureDisk(source, 3, &capture).Succeeded());
        }
        EXPECT_EQ(Context.Unmount(VFS::VFSPath::Root()).Error(), VFS::VFSError::AlreadyExists);
        Context.Shutdown();
        ASSERT_TRUE(Backend.InitializeOwned(ZKilo(512), 64).Succeeded());
        ExpectCapturedABC(capture);
        EXPECT_TRUE(capture.Reset().Succeeded());
    }

    TEST_F(CookCapturedFileTest, InitializationAndMissingStreamFailWithoutPublishingStorage)
    {
        CookCaptureStore store   = {};
        auto             context = CaptureContext();
        context.ScratchRun       = CaptureRelative("missing");
        EXPECT_EQ(store.Initialize(context, {}).Error(), VFS::VFSError::NotFound);
        context.ScratchRun = CaptureRelative("run");
        context.Allocator  = nullptr;
        EXPECT_EQ(store.Initialize(context, {}).Error(), VFS::VFSError::InvalidPath);
        context = CaptureContext();
        EXPECT_EQ(store.Initialize(context, {nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        uint8_t byte = 0;
        EXPECT_EQ(store.Initialize(context, {&byte, ZKilo(64) + 1}).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(store.Initialize(context, {}).Succeeded());
        EXPECT_EQ(store.Initialize(context, {}).Error(), VFS::VFSError::InvalidPath);
        CookCapturedFile capture = {};
        EXPECT_EQ(store.CaptureDisk(Source, 3, &capture).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(store.CaptureMemory(Source, 3, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_FALSE(capture.IsValid());
        EXPECT_EQ(store.OutstandingCaptures(), 0u);
        EXPECT_EQ(Allocator.Allocations, 1u);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, SourceDigestRechecksReuseTheStreamAndDetectSameSizeChanges)
    {
        CookCapturedFile capture = {};
        ASSERT_TRUE(Store.CaptureMemory(Source, 3, &capture).Succeeded());
        const auto calls = Allocator.Allocations;
        Source.MaxChunk  = 1;
        EXPECT_TRUE(Store.VerifySource(Source, capture.Size(), capture.Digest()).Succeeded());
        Source.Bytes[0] = 'z';
        EXPECT_EQ(Store.VerifySource(Source, capture.Size(), capture.Digest()).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Allocator.Allocations, calls);
        EXPECT_EQ(Store.LiveBytes(), 3u);
        EXPECT_EQ(Store.OutstandingCaptures(), 1u);
        EXPECT_EQ(Source.Closes, 0u);
        ExpectCapturedABC(capture);
        ExpectEmptyRun();
    }

    TEST_F(CookCapturedFileTest, SourceRechecksDistinguishChangedContentFromActualIOFailures)
    {
        CookCapturedFile capture = {};
        ASSERT_TRUE(Store.CaptureDisk(Source, 3, &capture).Succeeded());
        for (uint32_t fault = 0; fault < 9; ++fault)
        {
            CaptureSource source   = {};
            source.MaxChunk        = 1;
            VFS::VFSError expected = VFS::VFSError::Corrupted;
            if (fault == 0)
                source.StatLength = source.SizeLength = 2;
            if (fault == 1)
                source.Length = 4;
            if (fault == 2)
                source.StopAt = 2;
            if (fault == 3)
            {
                source.FailAt = 1;
                expected      = VFS::VFSError::PermissionDenied;
            }
            if (fault == 4)
            {
                source.StatError = VFS::VFSError::PermissionDenied;
                expected         = VFS::VFSError::PermissionDenied;
            }
            if (fault == 5)
            {
                source.ChangeSize  = true;
                source.FinalLength = 2;
            }
            if (fault == 6)
            {
                source.OversizedRead = true;
                expected             = VFS::VFSError::IOError;
            }
            if (fault == 7)
            {
                source.SizeError = VFS::VFSError::IOError;
                expected         = VFS::VFSError::IOError;
            }
            if (fault == 8)
            {
                source.Directory = true;
                expected         = VFS::VFSError::NotAFile;
            }
            EXPECT_EQ(Store.VerifySource(source, capture.Size(), capture.Digest()).Error(), expected) << fault;
            EXPECT_EQ(Allocator.Allocations, 1u);
            EXPECT_EQ(Store.OutstandingCaptures(), 1u);
            EXPECT_EQ(Store.LiveBytes(), 3u);
        }
        ExpectCapturedABC(capture);
    }

    TEST_F(CookCapturedFileTest, EmptySourceRecheckNeedsNoStreamButNonemptyRechecksCannotAllocateFallbackStorage)
    {
        CookCaptureStore store = {};
        EXPECT_EQ(store.VerifySource(Source, 3, {}).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(store.Initialize(CaptureContext(), {}).Succeeded());
        CookCapturedFile capture = {};
        ASSERT_TRUE(store.CaptureMemory(Source, 3, &capture).Succeeded());
        const auto calls = Allocator.Allocations;
        EXPECT_EQ(store.VerifySource(Source, capture.Size(), capture.Digest()).Error(), VFS::VFSError::SizeLimitExceeded);
        ASSERT_TRUE(capture.Reset().Succeeded());
        Source.Length = Source.StatLength = Source.SizeLength = 0;
        ASSERT_TRUE(store.CaptureMemory(Source, 0, &capture).Succeeded());
        EXPECT_TRUE(store.VerifySource(Source, capture.Size(), capture.Digest()).Succeeded());
        EXPECT_EQ(Allocator.Allocations, calls);
    }

    TEST_F(CookCapturedFileTest, SealRechecksContentBeforeReleasingExclusionAndCapturedBytesOutliveTheInputLease)
    {
        // Integration of real capture/recheck/exclusion/lease primitives. This
        // probe intentionally does not pretend to build a frozen disk catalog.
        struct SourceProbe final : ICookInputSource, Detail::ICookInputSession
        {
            CookLibraryExclusion* Owner    = nullptr;
            CookCaptureStore*     Store    = nullptr;
            VFS::IVFSFile*        File     = nullptr;
            CookLibraryReadLease  Read     = {};
            VFS::VFSRelativePath  Path     = {};
            uint64_t              Size     = 0;
            Digest256             Digest   = {};
            bool                  Captured = false;
            bool                  Leased   = false;

            VFS::VFSResult<void>  Acquire(const CookCaptureContext&, CookInputLease* output, CookDiagnostics*) override
            {
                if (!output || output->IsValid() || Leased)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
                const auto acquired = Owner->TryAcquireRead(&Read);
                if (acquired.Failed())
                    return acquired;
                const auto bound = BindLease(*this, output);
                if (bound.Succeeded())
                    Leased = true;
                else
                    Read.Reset();
                return bound;
            }
            VFS::VFSResult<void> EnumerateCatalog(CookCatalogVisitorFn, void*, CookDiagnostics*) override
            {
                return VFS::VFSResult<void>::Ok();
            }
            VFS::VFSResult<void> Capture(const VFS::VFSRelativePath& path, CookCapturedFile* output, CookDiagnostics*) override
            {
                const auto captured = Store->CaptureMemory(*File, 3, output);
                if (captured.Succeeded())
                {
                    Path     = path;
                    Size     = output->Size();
                    Digest   = output->Digest();
                    Captured = true;
                }
                return captured;
            }
            VFS::VFSResult<void> Seal(CookDiagnostics* diagnostics) override
            {
                const auto verified = Captured ? Store->VerifySource(*File, Size, Digest) : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (verified.Succeeded())
                    Read.Reset();
                else if (diagnostics)
                    diagnostics->Add({.Stage = CookStage::Capture, .Code = verified.Error() == VFS::VFSError::Corrupted ? CookDiagnosticCode::InputChanged : CookDiagnosticCode::IOFailure, .Underlying = verified.Error(), .Source = Path});
                return verified;
            }
            void Release() noexcept override
            {
                Read.Reset();
                Leased = false;
            }
        };

        for (bool change_before_seal : {false, true})
        {
            Source.Bytes[0]               = 'a';
            CookLibraryExclusion owner    = {};
            SourceProbe          provider = {};
            provider.Owner                = &owner;
            provider.Store                = &Store;
            provider.File                 = &Source;
            CookInputLease  lease         = {};
            CookDiagnostics diagnostics   = {};
            ASSERT_TRUE(provider.Acquire(CaptureContext(), &lease, &diagnostics).Succeeded());
            CookCapturedFile capture = {};
            ASSERT_TRUE(lease.Capture(CaptureRelative("assets/fixture.zemesh"), &capture, &diagnostics).Succeeded());
            CookLibraryMutationLease writer = {};
            EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), VFS::VFSError::AlreadyExists);
            const auto allocations = Allocator.Allocations;
            if (change_before_seal)
                Source.Bytes[0] = 'z'; // Bypassing the owner is detected, not retried.
            EXPECT_EQ(lease.Seal(&diagnostics).Error(), change_before_seal ? VFS::VFSError::Corrupted : VFS::VFSError::OK);
            EXPECT_EQ(lease.State(), change_before_seal ? CookInputState::Failed : CookInputState::Sealed);
            if (change_before_seal)
            {
                ASSERT_EQ(diagnostics.Count, 1u);
                EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::InputChanged);
                EXPECT_EQ(lease.Seal(&diagnostics).Error(), VFS::VFSError::InvalidPath);
            }
            EXPECT_EQ(Allocator.Allocations, allocations);
            lease.Reset();
            EXPECT_TRUE(owner.TryAcquireMutation(&writer).Succeeded());
            Source.Bytes[0] = 'q';
            ExpectCapturedABC(capture);
            EXPECT_EQ(Store.OutstandingCaptures(), 1u);
            EXPECT_TRUE(capture.Reset().Succeeded());
        }
    }

    class CookCaptureFaultTest : public CookCapturedFileTest
    {
    protected:
        CaptureFaults            Faults       = {};
        CaptureFaultBackend      FaultBackend = {};
        VFS::VFSScratchRootLease FaultScratch = {};

        void                     SetUp() override
        {
            CookCapturedFileTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            FaultBackend.Backend          = &Backend;
            FaultBackend.Provider         = new CaptureFaultProvider();
            FaultBackend.Provider->Inner  = Backend.RootProvider();
            FaultBackend.Provider->Faults = &Faults;
            ASSERT_TRUE(Context.Mount(&FaultBackend, CapturePath("/faults"), 0).Succeeded());
            ASSERT_TRUE(Context.AcquireScratchRoot(CapturePath("/faults/scratch"), &FaultScratch).Succeeded());
        }
        void TearDown() override
        {
            FaultScratch.Reset();
            CookCapturedFileTest::TearDown();
        }

        void InitializeFaultStore(CookCaptureStore& store)
        {
            auto context    = CaptureContext();
            context.Scratch = &FaultScratch;
            ASSERT_TRUE(store.Initialize(context, {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        }
    };

    TEST_F(CookCaptureFaultTest, DiskWritesHandleShortTransfersAndCloseBeforeExposure)
    {
        CookCaptureStore store = {};
        InitializeFaultStore(store);
        Faults.MaxWriteChunk     = 1;
        CookCapturedFile capture = {};
        ASSERT_TRUE(store.CaptureDisk(Source, 3, &capture).Succeeded());
        EXPECT_EQ(Faults.Writes, 3u);
        EXPECT_EQ(Faults.Flushes, 1u);
        EXPECT_EQ(Faults.WriterCloses, 1u);
        ExpectCapturedABC(capture);
        EXPECT_EQ(Faults.ReaderCloses, 1u);
        EXPECT_EQ(Allocator.Allocations, 1u);
    }

    TEST_F(CookCaptureFaultTest, WriteFlushAndCheckedCloseFailuresPreserveOutputAndCleanPendingFiles)
    {
        CookCaptureStore store = {};
        InitializeFaultStore(store);
        CookCapturedFile capture = {};
        ASSERT_TRUE(store.CaptureMemory(Source, 3, &capture).Succeeded());
        for (uint32_t fault = 0; fault < 5; ++fault)
        {
            Faults = {};
            if (fault == 0)
                Faults.WriteError = VFS::VFSError::PermissionDenied;
            if (fault == 1)
                Faults.MaxWriteChunk = 0;
            if (fault == 2)
                Faults.OversizedWrite = true;
            if (fault == 3)
                Faults.FlushError = VFS::VFSError::IOError;
            if (fault == 4)
                Faults.CloseError = VFS::VFSError::IOError;
            EXPECT_EQ(store.CaptureDisk(Source, 3, &capture).Error(), fault == 0 ? VFS::VFSError::PermissionDenied : VFS::VFSError::IOError);
            EXPECT_EQ(Faults.WriterCloses, 1u);
            EXPECT_EQ(Faults.Flushes, fault >= 3 ? 1u : 0u);
            ExpectCapturedABC(capture);
            EXPECT_EQ(store.LiveBytes(), 3u);
            EXPECT_EQ(store.OutstandingCaptures(), 1u);
            EXPECT_EQ(store.CleanupError(), VFS::VFSError::OK);
            ExpectEmptyRun();
        }
    }

    TEST_F(CookCaptureFaultTest, ReadErrorsAndCheckedReadCloseFailuresReleaseWrappers)
    {
        CookCaptureStore store = {};
        InitializeFaultStore(store);
        CookCapturedFile capture = {};
        ASSERT_TRUE(store.CaptureDisk(Source, 3, &capture).Succeeded());
        uint8_t bytes[8]  = {};
        Faults.ReadError  = VFS::VFSError::PermissionDenied;
        Faults.CloseError = VFS::VFSError::IOError;
        EXPECT_EQ(capture.Read(0, bytes).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Faults.ReaderCloses, 1u);
        Faults.ReadError = VFS::VFSError::OK;
        EXPECT_EQ(capture.Read(0, bytes).Error(), VFS::VFSError::IOError);
        EXPECT_EQ(Faults.ReaderCloses, 2u);
        Faults.CloseError = VFS::VFSError::OK;
        ExpectCapturedABC(capture);
        EXPECT_EQ(Faults.ReaderCloses, 3u);
    }

    TEST_F(CookCaptureFaultTest, CleanupErrorsRemainVisibleAndAbandonedBytesRemainCharged)
    {
        Limits.MaxCapturedBytes = 5;
        CookCaptureStore store  = {};
        InitializeFaultStore(store);
        CookCapturedFile capture = {};
        ASSERT_TRUE(store.CaptureDisk(Source, 3, &capture).Succeeded());
        Faults.RemoveError = VFS::VFSError::PermissionDenied;
        EXPECT_EQ(capture.Reset().Error(), VFS::VFSError::PermissionDenied);
        EXPECT_FALSE(capture.IsValid());
        EXPECT_EQ(store.CleanupError(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(store.LiveBytes(), 3u);
        EXPECT_EQ(store.OutstandingCaptures(), 0u);
        EXPECT_EQ(store.CaptureDisk(Source, 3, &capture).Error(), VFS::VFSError::SizeLimitExceeded);
        Faults.RemoveError = VFS::VFSError::OK;
        // Run cleanup can still remove only the owned orphan via the pinned root.
        ASSERT_TRUE(FaultScratch.Remove(CaptureRelative("run/capture-0000000000000000")).Succeeded());
        ExpectEmptyRun();
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    class CookNativeCaptureTest : public testing::Test
    {
    protected:
        ZEngine::Tests::TemporaryDirectory  Directory = {};
        Core::Memory::MemoryManager         Memory    = {};
        VFS::VFSScratchRootLease            Scratch   = {};
        Tests::FixedAllocator<ZKilo(16), 8> Allocator = {};
        CookWorkspace                       Stream    = {};
        CookLimits                          Limits    = {.MaxWorkingBytes = ZKilo(16)};
        CookCaptureStore                    Store     = {};

        void                                SetUp() override
        {
            std::error_code error = {};
            ASSERT_TRUE(Directory.Create("zengine_cook_capture", error));
            ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "scratch", error));
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Stream.Initialize(Allocator, 64, CookMemoryCategory::Captures).Succeeded());
            {
                // Both source VFS facade and context are destroyed before capture.
                VFS::VFSDiskBackend backend = {};
                const auto          native  = Directory.Path().generic_u8string();
                backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena);
                VFS::VFSContext context = {};
                context.Initialize(&Memory.MainArena);
                ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
                ASSERT_TRUE(context.AcquireScratchRoot(CapturePath("/scratch"), &Scratch).Succeeded());
                context.Shutdown();
            }
            ASSERT_TRUE(Scratch.CreateDirExclusive(CaptureRelative("run")).Succeeded());
            ASSERT_TRUE(Store.Initialize({&Scratch, CaptureRelative("run"), &Allocator, &Limits}, {static_cast<uint8_t*>(Stream.Arena()->m_memory), 64}).Succeeded());
        }
        void TearDown() override
        {
            EXPECT_EQ(Store.OutstandingCaptures(), 0u);
            EXPECT_EQ(Store.LiveBytes(), 0u);
            EXPECT_EQ(Store.CleanupError(), VFS::VFSError::OK);
            EXPECT_TRUE(Scratch.Remove(CaptureRelative("run")).Succeeded());
            Stream.Release();
            Scratch.Reset();
        }
    };

    TEST_F(CookNativeCaptureTest, MultiChunkDiskCaptureSurvivesFacadeDestructionAndUsesPinnedCleanup)
    {
        CaptureSource source = {};
        source.Length = source.StatLength = source.SizeLength = sizeof(source.Bytes);
        for (size_t i = 0; i < sizeof(source.Bytes); ++i)
            source.Bytes[i] = static_cast<uint8_t>(i);
        source.MaxChunk          = 7;
        CookCapturedFile capture = {};
        ASSERT_TRUE(Store.CaptureDisk(source, sizeof(source.Bytes), &capture).Succeeded());
        EXPECT_EQ(Allocator.Allocations, 1u);
        EXPECT_EQ(Store.LiveBytes(), sizeof(source.Bytes));
        // Renaming the acquired native directory cannot redirect existing roots.
        std::error_code error = {};
        std::filesystem::rename(Directory.Path() / "scratch", Directory.Path() / "retained", error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directory(Directory.Path() / "scratch", error));
        uint8_t    output[1024] = {};
        const auto read         = capture.Read(0, output);
        ASSERT_TRUE(read.Succeeded());
        ASSERT_EQ(read.Value(), sizeof(output));
        EXPECT_EQ(Helpers::secure_memcmp(output, sizeof(output), source.Bytes, sizeof(source.Bytes), sizeof(output)), 0);
        EXPECT_TRUE(capture.Reset().Succeeded());
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "retained/run"));
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "scratch"));
    }

    TEST_F(CookNativeCaptureTest, FailedSourceReadRemovesTheOwnedNativeTemporary)
    {
        CaptureSource source     = {};
        source.MaxChunk          = 1;
        source.FailAt            = 1;
        CookCapturedFile capture = {};
        EXPECT_EQ(Store.CaptureDisk(source, 3, &capture).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_FALSE(capture.IsValid());
        EXPECT_TRUE(std::filesystem::is_empty(Directory.Path() / "scratch/run"));
    }
#endif
} // namespace ZEngine::Cook
