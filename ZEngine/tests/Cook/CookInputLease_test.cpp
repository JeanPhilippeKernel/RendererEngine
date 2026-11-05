#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookInputLease.h>
#include <ZEngine/Cook/CookLibraryExclusion.h>
#include <gtest/gtest.h>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace
    {
        // State-machine probe, not a substitute for a real frozen-library
        // provider's catalog construction, capture, or digest/identity rechecks.
        struct LeaseProbe final : ICookInputSource, Detail::ICookInputSession
        {
            CookLibraryExclusion*      Owner        = nullptr;
            CookLibraryReadLease       ReadLease    = {};
            CookAssetCatalogEntry      Entry        = {};
            Core::VFS::VFSRelativePath Mapping      = {};
            Core::VFS::VFSError        MappingError = Core::VFS::VFSError::Unsupported;
            Core::VFS::VFSError        LookupError  = Core::VFS::VFSError::Unsupported;
            Core::VFS::VFSError        SealError    = Core::VFS::VFSError::OK;
            Core::VFS::VFSError        CaptureError = Core::VFS::VFSError::NotFound;
            Core::VFS::VFSError        PrepareError = Core::VFS::VFSError::OK;
            uint32_t                   Preparations = 0;
            uint32_t                   Enumerations = 0;
            uint32_t                   Captures     = 0;
            uint32_t                   Seals        = 0;
            uint32_t                   Releases     = 0;
            uint32_t                   Mappings     = 0;
            uint32_t                   Lookups      = 0;
            bool                       Leased       = false;

            ~LeaseProbe() override
            {
                EXPECT_FALSE(Leased);
            }

            Core::VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease* output, CookDiagnostics*) override
            {
                return AcquireState(output, CookInputState::Active);
            }
            Core::VFS::VFSResult<void> AcquireRoots(CookInputLease* output, CookDiagnostics*) override
            {
                return AcquireState(output, CookInputState::Rooted);
            }
            Core::VFS::VFSResult<void> PrepareCapture(const CookCaptureContext&, CookDiagnostics*) override
            {
                ++Preparations;
                return PrepareError == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(PrepareError);
            }
            Core::VFS::VFSResult<void> AcquireState(CookInputLease* output, CookInputState initial)
            {
                if (!output || !Owner)
                    return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
                if (Leased || output->IsValid())
                    return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::AlreadyExists);
                const auto acquired = Owner->TryAcquireRead(&ReadLease);
                if (acquired.Failed())
                    return acquired;
                const auto bound = BindLease(*this, output, initial);
                if (bound.Failed())
                    ReadLease.Reset();
                else
                    Leased = true;
                return bound;
            }
            Core::VFS::VFSResult<void> MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath* output, CookDiagnostics*) override
            {
                ++Mappings;
                *output = Mapping; // Deliberately write even on error to test transactional copy-out.
                return MappingError == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(MappingError);
            }
            Core::VFS::VFSResult<void> FindAsset(const CookAssetIdentity&, CookAssetCatalogEntry* output, CookDiagnostics*) override
            {
                ++Lookups;
                *output = Entry;
                return LookupError == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(LookupError);
            }
            Core::VFS::VFSResult<void> EnumerateCatalog(CookCatalogVisitorFn visitor, void* context, CookDiagnostics*) override
            {
                ++Enumerations;
                return visitor(context, Entry);
            }
            Core::VFS::VFSResult<void> Capture(const Core::VFS::VFSRelativePath&, CookCapturedFile*, CookDiagnostics*) override
            {
                ++Captures;
                return CaptureError == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(CaptureError);
            }
            Core::VFS::VFSResult<void> Seal(CookDiagnostics* diagnostics) override
            {
                ++Seals;
                if (SealError == Core::VFS::VFSError::OK)
                {
                    ReadLease.Reset();
                    return Core::VFS::VFSResult<void>::Ok();
                }
                if (diagnostics)
                    diagnostics->Add({.Stage = CookStage::Capture, .Code = SealError == Core::VFS::VFSError::Corrupted ? CookDiagnosticCode::InputChanged : CookDiagnosticCode::IOFailure, .Underlying = SealError});
                return Core::VFS::VFSResult<void>::Fail(SealError);
            }
            void Release() noexcept override
            {
                ++Releases;
                ReadLease.Reset();
                Leased = false;
            }
        };

        Core::VFS::VFSResult<void> CountCatalog(void* context, const CookAssetCatalogEntry&)
        {
            ++*static_cast<uint32_t*>(context);
            return Core::VFS::VFSResult<void>::Ok();
        }
    } // namespace

    TEST(CookInputLeaseTest, DefaultLeaseHasNoAuthorityAndResetDoesNotValidate)
    {
        static_assert(!std::is_copy_constructible_v<CookInputLease>);
        static_assert(std::is_nothrow_move_assignable_v<CookInputLease>);
        static_assert(!std::is_constructible_v<CookInputLease, bool>);
        CookInputLease  lease       = {};
        CookDiagnostics diagnostics = {};
        EXPECT_FALSE(lease.IsValid());
        EXPECT_EQ(lease.State(), CookInputState::Invalid);
        EXPECT_EQ(lease.Seal(&diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.EnumerateCatalog(nullptr, nullptr, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.Capture({}, nullptr, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(diagnostics.ErrorCount, 3u);
        for (size_t i = 0; i < diagnostics.Count; ++i)
            EXPECT_EQ(diagnostics.Entries[i].Code, CookDiagnosticCode::InvalidRequest);
        lease.Reset();
        lease.Reset();
    }

    TEST(CookInputLeaseTest, SuccessfulAndFailedSealsAreTerminalAndCannotBeRetried)
    {
        for (auto error : {Core::VFS::VFSError::OK, Core::VFS::VFSError::Corrupted, Core::VFS::VFSError::PermissionDenied})
        {
            CookLibraryExclusion owner  = {};
            LeaseProbe           source = {};
            source.Owner                = &owner;
            source.SealError            = error;
            CookInputLease  lease       = {};
            CookDiagnostics diagnostics = {};
            ASSERT_TRUE(source.Acquire({}, &lease, &diagnostics).Succeeded());
            EXPECT_TRUE(lease.IsValid());
            EXPECT_EQ(lease.State(), CookInputState::Active);
            uint32_t count = 0;
            ASSERT_TRUE(lease.EnumerateCatalog(CountCatalog, &count, &diagnostics).Succeeded());
            EXPECT_EQ(count, 1u);
            EXPECT_EQ(lease.Seal(&diagnostics).Error(), error);
            EXPECT_EQ(lease.State(), error == Core::VFS::VFSError::OK ? CookInputState::Sealed : CookInputState::Failed);
            EXPECT_EQ(source.Seals, 1u);
            EXPECT_EQ(lease.Seal(&diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
            EXPECT_EQ(lease.EnumerateCatalog(CountCatalog, &count, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
            CookCapturedFile capture = {};
            const auto       path    = Core::VFS::VFSRelativePath::Parse("assets/mesh.zemesh").Value();
            EXPECT_EQ(lease.Capture(path, &capture, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
            EXPECT_EQ(source.Seals, 1u);
            EXPECT_EQ(source.Enumerations, 1u);
            EXPECT_EQ(source.Captures, 0u);
            lease.Reset();
            EXPECT_EQ(source.Releases, 1u);
            EXPECT_EQ(source.Seals, 1u);
            EXPECT_EQ(lease.State(), CookInputState::Invalid);
            CookLibraryMutationLease writer = {};
            EXPECT_TRUE(owner.TryAcquireMutation(&writer).Succeeded());
        }
    }

    TEST(CookInputLeaseTest, RootedLeaseCannotCaptureEnumerateOrSealUntilItsSinglePreparationSucceeds)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        CookInputLease lease        = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, nullptr).Succeeded());
        EXPECT_EQ(lease.State(), CookInputState::Rooted);
        uint32_t         count   = 0;
        CookCapturedFile capture = {};
        EXPECT_EQ(lease.EnumerateCatalog(CountCatalog, &count, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.Capture(Core::VFS::VFSRelativePath::Parse("scenes/main.yaml").Value(), &capture, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.Seal(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(source.Enumerations + source.Captures + source.Seals, 0u);
        CookInputLease moved(std::move(lease));
        EXPECT_EQ(lease.State(), CookInputState::Invalid);
        EXPECT_EQ(moved.State(), CookInputState::Rooted);
        ASSERT_TRUE(moved.PrepareCapture({}, nullptr).Succeeded());
        EXPECT_EQ(moved.State(), CookInputState::Active);
        EXPECT_EQ(moved.PrepareCapture({}, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(source.Preparations, 1u);
        ASSERT_TRUE(moved.EnumerateCatalog(CountCatalog, &count, nullptr).Succeeded());
        ASSERT_TRUE(moved.Seal(nullptr).Succeeded());
        EXPECT_EQ(count, 1u);
    }

    TEST(CookInputLeaseTest, FailedRootedPreparationCannotRetryAndResetReleasesExclusionWithoutSeal)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        source.PrepareError         = Core::VFS::VFSError::SizeLimitExceeded;
        CookInputLease lease        = {};
        ASSERT_TRUE(source.AcquireRoots(&lease, nullptr).Succeeded());
        EXPECT_EQ(lease.PrepareCapture({}, nullptr).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(lease.State(), CookInputState::Failed);
        EXPECT_EQ(lease.PrepareCapture({}, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.Seal(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(source.Preparations, 1u);
        EXPECT_EQ(source.Seals, 0u);
        CookLibraryMutationLease mutation = {};
        EXPECT_EQ(owner.TryAcquireMutation(&mutation).Error(), Core::VFS::VFSError::AlreadyExists);
        lease.Reset();
        EXPECT_EQ(source.Releases, 1u);
        EXPECT_TRUE(owner.TryAcquireMutation(&mutation).Succeeded());
    }

    TEST(CookInputLeaseTest, AbandonmentReleasesExclusionWithoutPerformingTheFirstSeal)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        source.SealError            = Core::VFS::VFSError::Corrupted;
        {
            CookInputLease lease = {};
            ASSERT_TRUE(source.Acquire({}, &lease, nullptr).Succeeded());
            CookLibraryMutationLease writer = {};
            EXPECT_EQ(owner.TryAcquireMutation(&writer).Error(), Core::VFS::VFSError::AlreadyExists);
        }
        EXPECT_EQ(source.Seals, 0u);
        EXPECT_EQ(source.Releases, 1u);
        CookLibraryMutationLease writer = {};
        EXPECT_TRUE(owner.TryAcquireMutation(&writer).Succeeded());
    }

    TEST(CookInputLeaseTest, CaptureValidatesLogicalPathsAndRejectsSuccessWithoutOwnedStorage)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        CookInputLease  lease       = {};
        CookDiagnostics diagnostics = {};
        ASSERT_TRUE(source.Acquire({}, &lease, &diagnostics).Succeeded());
        CookCapturedFile output = {};
        EXPECT_EQ(lease.Capture(Core::VFS::VFSRelativePath::Parse("assets").Value(), &output, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(lease.Capture(Core::VFS::VFSRelativePath::Parse("Assets/mesh").Value(), &output, &diagnostics).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidPath);
        EXPECT_EQ(source.Captures, 0u);
        const auto path = Core::VFS::VFSRelativePath::Parse("assets/mesh").Value();
        EXPECT_EQ(lease.Capture(path, &output, &diagnostics).Error(), Core::VFS::VFSError::NotFound);
        source.CaptureError = Core::VFS::VFSError::OK;
        EXPECT_EQ(lease.Capture(path, &output, &diagnostics).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_FALSE(output.IsValid());
        EXPECT_EQ(source.Captures, 2u);
        EXPECT_EQ(lease.State(), CookInputState::Active);
    }

    TEST(CookInputLeaseTest, MappingAndLookupValidateProviderSuccessAndPreserveOutputsOnFailure)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        CookInputLease lease        = {};
        ASSERT_TRUE(source.Acquire({}, &lease, nullptr).Succeeded());
        const auto path     = Core::VFS::VFSPath::Parse("/scenes/main.yaml").Value();
        const auto original = Core::VFS::VFSRelativePath::Parse("previous/output").Value();
        auto       mapped   = original;
        EXPECT_EQ(lease.MapContextPath(path, &mapped, nullptr).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(mapped, original);
        source.MappingError = Core::VFS::VFSError::OK;
        source.Mapping      = Core::VFS::VFSRelativePath::Parse("scenes").Value();
        EXPECT_EQ(lease.MapContextPath(path, &mapped, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(mapped, original);
        source.Mapping = Core::VFS::VFSRelativePath::Parse("scenes/main.yaml").Value();
        EXPECT_TRUE(lease.MapContextPath(path, &mapped, nullptr).Succeeded());
        EXPECT_EQ(mapped, source.Mapping);
        const CookAssetIdentity     identity = {.Kind = CookAssetKind::Texture, .UUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value()};
        const CookAssetCatalogEntry previous = {.Identity = identity, .Artifact = original};
        auto                        found    = previous;
        EXPECT_EQ(lease.FindAsset(identity, &found, nullptr).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(found.Artifact, previous.Artifact);
        source.LookupError = Core::VFS::VFSError::OK;
        source.Entry       = {.Identity = identity, .Artifact = Core::VFS::VFSRelativePath::Parse("assets/a.png").Value(), .Metadata = Core::VFS::VFSRelativePath::Parse("assets/a.png.meta").Value()};
        const auto valid   = source.Entry;
        for (uint32_t invalid = 0; invalid < 5; ++invalid)
        {
            source.Entry = valid;
            if (invalid == 0)
                source.Entry.Identity = {};
            if (invalid == 1)
                source.Entry.Artifact = {};
            if (invalid == 2)
                source.Entry.Metadata = source.Entry.Artifact;
            if (invalid == 3)
                source.Entry.EnvironmentSource = original;
            if (invalid == 4)
                source.Entry.Metadata = {};
            EXPECT_EQ(lease.FindAsset(identity, &found, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
            EXPECT_EQ(found.Identity, previous.Identity);
            EXPECT_EQ(found.Artifact, previous.Artifact);
        }
        source.Entry = valid;
        EXPECT_TRUE(lease.FindAsset(identity, &found, nullptr).Succeeded());
        EXPECT_EQ(found.Artifact, valid.Artifact);
        const auto lookups  = source.Lookups;
        const auto mappings = source.Mappings;
        EXPECT_TRUE(lease.MapContextPath({}, &mapped, nullptr).Failed());
        EXPECT_TRUE(lease.FindAsset({}, &found, nullptr).Failed());
        EXPECT_TRUE(lease.MapContextPath(path, nullptr, nullptr).Failed());
        EXPECT_TRUE(lease.FindAsset(identity, nullptr, nullptr).Failed());
        EXPECT_EQ(source.Lookups, lookups);
        EXPECT_EQ(source.Mappings, mappings);
    }

    TEST(CookInputLeaseTest, DependencyResolutionPreservesProviderFailuresAndPriorOutput)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        CookInputLease lease        = {};
        ASSERT_TRUE(source.Acquire({}, &lease, nullptr).Succeeded());
        const CookAssetIdentity identity = {CookAssetKind::Texture, uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value()};
        struct FailureCase
        {
            Core::VFS::VFSError Error = Core::VFS::VFSError::OK;
            CookDiagnosticCode  Code  = CookDiagnosticCode::None;
        };
        const FailureCase failures[] = {
            {         Core::VFS::VFSError::NotFound,  CookDiagnosticCode::MissingDependency},
            {      Core::VFS::VFSError::OutOfMemory,   CookDiagnosticCode::AllocationFailed},
            {Core::VFS::VFSError::SizeLimitExceeded,     CookDiagnosticCode::BudgetExceeded},
            {        Core::VFS::VFSError::Cancelled,          CookDiagnosticCode::IOFailure},
            { Core::VFS::VFSError::PermissionDenied,          CookDiagnosticCode::IOFailure},
            {      Core::VFS::VFSError::Unsupported, CookDiagnosticCode::UnsupportedBackend},
            {      Core::VFS::VFSError::InvalidPath,        CookDiagnosticCode::InvalidPath},
            {        Core::VFS::VFSError::Corrupted,       CookDiagnosticCode::InvalidAsset},
        };
        for (const auto& failure : failures)
        {
            source.MappingError = source.LookupError = failure.Error;
            for (cstring path : {"/assets/a.png", "assets/a.png"})
            {
                CookDiagnostics       diagnostics = {};
                CookAssetCatalogEntry previous    = {
                    .Identity = {CookAssetKind::Mesh, identity.UUID}
                };
                const CookDependency dependency = {identity, "albedo", path};
                EXPECT_EQ(ResolveCookDependency(lease, dependency, &previous, &diagnostics).Error(), failure.Error);
                EXPECT_EQ(previous.Identity.Kind, CookAssetKind::Mesh);
                ASSERT_EQ(diagnostics.Count, 1u);
                EXPECT_EQ(diagnostics.Entries[0].Code, failure.Code);
                EXPECT_EQ(diagnostics.Entries[0].Underlying, failure.Error);
                EXPECT_EQ(diagnostics.Entries[0].Asset, identity);
                EXPECT_STREQ(diagnostics.Entries[0].FieldKey, "albedo");
            }
        }
    }

    TEST(CookInputLeaseTest, MappingAndLookupCannotAccessProvidersAfterEitherTerminalStateOrReset)
    {
        for (const auto error : {Core::VFS::VFSError::OK, Core::VFS::VFSError::Corrupted})
        {
            CookLibraryExclusion owner  = {};
            LeaseProbe           source = {};
            source.Owner                = &owner;
            source.SealError            = error;
            CookInputLease lease        = {};
            ASSERT_TRUE(source.Acquire({}, &lease, nullptr).Succeeded());
            EXPECT_EQ(lease.Seal(nullptr).Error(), error);
            const auto                 path     = Core::VFS::VFSPath::Parse("/scenes/main.yaml").Value();
            const CookAssetIdentity    identity = {.Kind = CookAssetKind::Texture, .UUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value()};
            Core::VFS::VFSRelativePath mapped   = {};
            CookAssetCatalogEntry      found    = {};
            for (uint32_t state = 0; state < 2; ++state)
            {
                EXPECT_TRUE(lease.MapContextPath(path, &mapped, nullptr).Failed());
                EXPECT_TRUE(lease.FindAsset(identity, &found, nullptr).Failed());
                lease.Reset();
            }
            EXPECT_EQ(source.Lookups, 0u);
            EXPECT_EQ(source.Mappings, 0u);
        }
    }

    TEST(CookInputLeaseTest, EnumerationFailureIsPreservedWithoutImplicitSealing)
    {
        CookLibraryExclusion owner  = {};
        LeaseProbe           source = {};
        source.Owner                = &owner;
        CookInputLease lease        = {};
        ASSERT_TRUE(source.Acquire({}, &lease, nullptr).Succeeded());
        const auto fail = [](void*, const CookAssetCatalogEntry&) { return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded); };
        EXPECT_EQ(lease.EnumerateCatalog(fail, nullptr, nullptr).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(lease.EnumerateCatalog(nullptr, nullptr, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(source.Enumerations, 1u);
        EXPECT_EQ(source.Seals, 0u);
        EXPECT_EQ(lease.State(), CookInputState::Active);
    }

    TEST(CookInputLeaseTest, MovesTransferAuthorityAndReleaseOverwrittenSessionsExactlyOnce)
    {
        CookLibraryExclusion owner = {};
        LeaseProbe           first = {}, second = {};
        first.Owner = second.Owner = &owner;
        CookInputLease one = {}, two = {};
        ASSERT_TRUE(first.Acquire({}, &one, nullptr).Succeeded());
        ASSERT_TRUE(second.Acquire({}, &two, nullptr).Succeeded());
        two = std::move(one);
        EXPECT_FALSE(one.IsValid());
        EXPECT_EQ(second.Releases, 1u);
        EXPECT_EQ(first.Releases, 0u);
        ASSERT_TRUE(two.Seal(nullptr).Succeeded());
        CookInputLease moved(std::move(two));
        EXPECT_EQ(moved.State(), CookInputState::Sealed);
        EXPECT_EQ(two.State(), CookInputState::Invalid);
        auto* same = &moved;
        moved      = std::move(*same);
        EXPECT_TRUE(moved.IsValid());
        moved.Reset();
        EXPECT_EQ(first.Releases, 1u);
        EXPECT_EQ(first.Seals, 1u);
        EXPECT_EQ(second.Seals, 0u);
    }

    TEST(CookInputLeaseTest, BusyAcquisitionPreservesTheExistingLeaseAndCanRecoverAfterMutation)
    {
        CookLibraryExclusion owner = {};
        LeaseProbe           first = {}, second = {};
        first.Owner = second.Owner        = &owner;
        CookInputLease           lease    = {};
        CookLibraryMutationLease mutation = {};
        ASSERT_TRUE(owner.TryAcquireMutation(&mutation).Succeeded());
        EXPECT_EQ(first.Acquire({}, &lease, nullptr).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_FALSE(lease.IsValid());
        mutation.Reset();
        ASSERT_TRUE(first.Acquire({}, &lease, nullptr).Succeeded());
        EXPECT_EQ(second.Acquire({}, &lease, nullptr).Error(), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(lease.State(), CookInputState::Active);
        EXPECT_EQ(first.Releases, 0u);
        EXPECT_FALSE(second.Leased);
    }
} // namespace ZEngine::Cook
