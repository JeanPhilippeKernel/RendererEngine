#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Primitives/ImageMemoryBarrier.h>
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>
#include <ZEngine/Rendering/Renderers/Base/ITransferPass.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyEnvironmentBakePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <ZEngine/Rendering/Renderers/SceneDescriptorRecipes.h>
#include <ZEngine/Rendering/Renderers/SelectionOutlineFrame.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <gtest/gtest.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering::Renderers;
using ZEngine::Rendering::Specifications::AttachmentSpecification;
using ZEngine::Rendering::Specifications::ImageFormat;
using ZEngine::Rendering::Specifications::ImageLayout;
using ZEngine::Rendering::Specifications::LoadOperation;
using ZEngine::Rendering::Specifications::PipelineBindPoint;
using ZEngine::Rendering::Specifications::StoreOperation;

TEST(RenderGraphSceneDescriptorRecipeTest, ExactImagesAreScopedByPassAndDescriptorKind)
{
    using Type = Rendering::Specifications::DescriptorType;
    struct Expected
    {
        RGSceneDescriptorProfile Profile = RGSceneDescriptorProfile::None;
        const char*              Name    = nullptr;
        SceneRenderTarget        Target  = SceneRenderTarget::Count;
        Type                     Kind    = Type::SAMPLED_IMAGE;
    };
    const Expected bindings[] = {
        {RGSceneDescriptorProfile::Lighting, "GBufferAlbedoAO", SceneRenderTarget::AlbedoAO},
        {RGSceneDescriptorProfile::Lighting, "GBufferNormalRoughness", SceneRenderTarget::NormalRoughness},
        {RGSceneDescriptorProfile::Lighting, "GBufferMetallicEmissive", SceneRenderTarget::MetallicEmissive},
        {RGSceneDescriptorProfile::Lighting, "GBufferDepth", SceneRenderTarget::Depth},
        {RGSceneDescriptorProfile::Grid, "SceneDepth", SceneRenderTarget::Depth},
        {RGSceneDescriptorProfile::GridLabels, "SceneDepth", SceneRenderTarget::Depth},
        {RGSceneDescriptorProfile::SelectionOutline, "ObjectId", SceneRenderTarget::ObjectId},
        {RGSceneDescriptorProfile::SkyComposite, "OpaqueSceneColor", SceneRenderTarget::HdrColor},
        {RGSceneDescriptorProfile::SkyComposite, "SceneDepth", SceneRenderTarget::Depth},
        {RGSceneDescriptorProfile::SkyComposite, "SkyViewLut", SceneRenderTarget::SkyViewLut},
        {RGSceneDescriptorProfile::SkyComposite, "AerialInscatter", SceneRenderTarget::AerialInscatter},
        {RGSceneDescriptorProfile::SkyComposite, "AerialTransmittance", SceneRenderTarget::AerialTransmittance},
        {RGSceneDescriptorProfile::SkyViewLut, "Destination", SceneRenderTarget::SkyViewLut, Type::STORAGE_IMAGE},
        {RGSceneDescriptorProfile::AerialPerspective, "InscatterDestination", SceneRenderTarget::AerialInscatter, Type::STORAGE_IMAGE},
        {RGSceneDescriptorProfile::AerialPerspective, "TransmittanceDestination", SceneRenderTarget::AerialTransmittance, Type::STORAGE_IMAGE},
    };
    for (const auto& binding : bindings)
    {
        SCOPED_TRACE(binding.Name);
        EXPECT_EQ(ResolveSceneDescriptorTarget(binding.Profile, binding.Name, binding.Kind, false), binding.Target);
        EXPECT_EQ(ResolveSceneDescriptorTarget(RGSceneDescriptorProfile::Geometry, binding.Name, binding.Kind, false), SceneRenderTarget::Count);
        EXPECT_EQ(ResolveSceneDescriptorTarget(binding.Profile, binding.Name, binding.Kind == Type::SAMPLED_IMAGE ? Type::STORAGE_IMAGE : Type::SAMPLED_IMAGE, false), SceneRenderTarget::Count);
    }
    EXPECT_EQ(ResolveSceneDescriptorTarget(RGSceneDescriptorProfile::ToneMapping, "SceneColor", Type::SAMPLED_IMAGE, false), SceneRenderTarget::HdrColor);
    EXPECT_EQ(ResolveSceneDescriptorTarget(RGSceneDescriptorProfile::ToneMapping, "SceneColor", Type::SAMPLED_IMAGE, true), SceneRenderTarget::HdrComposited);
    EXPECT_EQ(ResolveSceneDescriptorTarget(RGSceneDescriptorProfile::ToneMapping, nullptr, Type::SAMPLED_IMAGE, true), SceneRenderTarget::Count);
    EXPECT_EQ(ResolveSceneDescriptorTarget(RGSceneDescriptorProfile::Lighting, "Unknown", Type::SAMPLED_IMAGE, false), SceneRenderTarget::Count);
}

TEST(RenderGraphSceneDescriptorRecipeTest, EnvironmentAndSamplersDoNotLeakAcrossProfiles)
{
    Rendering::RenderResourceManager::TextureVersionPin pins[6]     = {};
    const SceneDescriptorEnvironment                    environment = {&pins[0], &pins[1], &pins[2], &pins[3], &pins[4], &pins[5], false, &pins[0]};
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Lighting, "DiffuseIrradiance", environment), &pins[1]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Lighting, "SpecularEnvironment", environment), &pins[2]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Lighting, "BrdfIntegrationLut", environment), &pins[3]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::SkyViewLut, "Transmittance", environment), &pins[4]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::AerialPerspective, "Multiscattering", environment), &pins[5]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::SkyComposite, "AtmosphereTransmittance", environment), &pins[4]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Grid, "DiffuseIrradiance", environment), nullptr);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Lighting, "Transmittance", environment), nullptr);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::ToneMapping, "SceneColor", environment), nullptr);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::GridLabels, "FontAtlas", environment), &pins[0]);
    EXPECT_EQ(ResolveSceneDescriptorEnvironment(RGSceneDescriptorProfile::Grid, "FontAtlas", environment), nullptr);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::Lighting, "GBufferSampler"), SceneDescriptorSampler::Wrap);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::Lighting, "EnvironmentSampler"), SceneDescriptorSampler::Clamp);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::Grid, "DepthSampler"), SceneDescriptorSampler::Clamp);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::GridLabels, "AtlasSampler"), SceneDescriptorSampler::Clamp);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::SelectionOutline, "ObjectIdSampler"), SceneDescriptorSampler::NearestClamp);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::SkyComposite, "SkyViewSampler"), SceneDescriptorSampler::SkyView);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::Grid, "SkyViewSampler"), SceneDescriptorSampler::None);
    EXPECT_EQ(ResolveSceneDescriptorSampler(RGSceneDescriptorProfile::ToneMapping, "Unknown"), SceneDescriptorSampler::None);
}

TEST(RenderGraphSceneTargetTest, SchemaUsesDistinctNamesAndExactStorage)
{
    uint32_t mask = 0;
    for (uint32_t index = 0; index < static_cast<uint32_t>(SceneRenderTarget::Count); ++index)
    {
        const auto target      = static_cast<SceneRenderTarget>(index);
        const auto declaration = DescribeSceneTarget(target, 640, 480);
        ASSERT_NE(declaration.Name, nullptr);
        EXPECT_EQ(mask & SceneTargetMask(target), 0u);
        mask                          |= SceneTargetMask(target);
        const bool atmosphere_storage  = target == SceneRenderTarget::SkyViewLut || target == SceneRenderTarget::AerialInscatter || target == SceneRenderTarget::AerialTransmittance;
        const bool aerial              = target == SceneRenderTarget::AerialInscatter || target == SceneRenderTarget::AerialTransmittance;
        EXPECT_EQ(declaration.Spec.Width, atmosphere_storage ? (aerial ? SceneAerialResolution : SceneSkyViewWidth) : 640u);
        EXPECT_EQ(declaration.Spec.Height, atmosphere_storage ? (aerial ? SceneAerialResolution : SceneSkyViewHeight) : 480u);
        EXPECT_EQ(declaration.Spec.Depth, aerial ? SceneAerialResolution : 1u);
        EXPECT_EQ(declaration.Spec.Is3D, aerial);
        EXPECT_EQ(declaration.Spec.IsRenderTargetSized, !atmosphere_storage);
        EXPECT_TRUE(declaration.Spec.IsAliasable);
        EXPECT_TRUE(declaration.Spec.IsUsageSampled);
        EXPECT_EQ(declaration.Spec.IsUsageStorage, atmosphere_storage);
        const bool atmosphere = atmosphere_storage || target == SceneRenderTarget::HdrComposited;
        EXPECT_EQ(declaration.Spec.IsUsageTransferSource, !atmosphere);
        EXPECT_EQ(declaration.Spec.IsUsageTransfert, !atmosphere);
        if (atmosphere)
        {
            EXPECT_EQ(declaration.Spec.BytePerPixel, 8u);
            EXPECT_EQ(declaration.Spec.Format, Rendering::Specifications::ImageFormat::R16G16B16A16_SFLOAT);
        }
        for (uint32_t other = 0; other < index; ++other)
        {
            EXPECT_STRNE(declaration.Name, DescribeSceneTarget(static_cast<SceneRenderTarget>(other), 640, 480).Name);
        }
    }
    EXPECT_EQ(DescribeSceneTarget(SceneRenderTarget::NormalRoughness, 1, 1).Spec.BytePerPixel, 8u);
    EXPECT_EQ(DescribeSceneTarget(SceneRenderTarget::HdrColor, 1, 1).Spec.BytePerPixel, 8u);
    EXPECT_EQ(DescribeSceneTarget(SceneRenderTarget::ObjectId, 1, 1).Spec.Format, Rendering::Specifications::ImageFormat::R32_UINT);
}

TEST(RenderGraphSceneTargetTest, TransientPoolReusesRetiredSlotMetadata)
{
    MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    RGTransientPool pool = {};
    pool.Initialize(&memory.MainArena);
    const auto declaration = DescribeSceneTarget(SceneRenderTarget::Color, 64, 64);
    auto*      original    = pool.Register({1, 2}, declaration.Spec, 1);
    pool.RegisterAlias(original, declaration.Name, original->Handle, declaration.Spec, 0, 1);
    const auto offset = memory.MainArena.m_current_offset;
    original->Handle  = {};
    original->Aliases.clear();
    auto* replacement = pool.Register({2, 3}, declaration.Spec, 1);
    EXPECT_EQ(replacement, original);
    EXPECT_EQ(pool.Slots.size(), 1u);
    pool.RegisterAlias(replacement, declaration.Name, replacement->Handle, declaration.Spec, 0, 1);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(pool.FindNamedAlias(declaration.Name, declaration.Spec, 0, 1), (Rendering::Textures::TextureHandle{})); // Already active.
    pool.BeginFrame();
    EXPECT_EQ(pool.FindNamedAlias(declaration.Name, declaration.Spec, 0, 1), replacement->Handle);
}

TEST(RenderGraphScenePipelineTest, AttachmentProfileChecksCountFormatAndDepthOrder)
{
    MemoryManager memory = {};
    memory.Initialize(16384, {});
    Rendering::Specifications::AttachmentSpecification attachment = {};
    attachment.ColorsMap.init(&memory.MainArena, RGScenePipelineProfile::MaxAttachments);
    const RGScenePipelineProfile profile = {
        .Usage           = RGScenePipelineUsage::Core,
        .Formats         = {Rendering::Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE, Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM},
        .AttachmentCount = 2,
    };
    EXPECT_FALSE(profile.Matches(attachment));
    attachment.ColorsMap[0] = {.Format = profile.Formats[0]};
    EXPECT_FALSE(profile.Matches(attachment));
    attachment.ColorsMap[1] = {.Format = profile.Formats[1]};
    EXPECT_TRUE(profile.Matches(attachment));
    std::swap(attachment.ColorsMap[0], attachment.ColorsMap[1]);
    EXPECT_FALSE(profile.Matches(attachment));
    std::swap(attachment.ColorsMap[0], attachment.ColorsMap[1]);
    attachment.ColorsMap[1].Format = Rendering::Specifications::ImageFormat::R16G16B16A16_SFLOAT;
    EXPECT_FALSE(profile.Matches(attachment));
    auto invalid            = profile;
    invalid.AttachmentCount = 0;
    EXPECT_FALSE(invalid.Matches(attachment));
    invalid.AttachmentCount = RGScenePipelineProfile::MaxAttachments + 1;
    EXPECT_FALSE(invalid.Matches(attachment));
}

TEST(RenderGraphScenePipelineTest, AttachmentRecipesAreExplicitOrderedAndOnlyRedirectOptedInHdr)
{
    RGScenePipelineProfile profile = {
        .AttachmentCount = 2, .AttachmentTargets = {SceneRenderTarget::Depth, SceneRenderTarget::HdrColor},
             .CompositedHdrAttachment = true
    };
    EXPECT_EQ(profile.ResolveAttachmentTarget(0, true), SceneRenderTarget::Depth);
    EXPECT_EQ(profile.ResolveAttachmentTarget(1, false), SceneRenderTarget::HdrColor);
    EXPECT_EQ(profile.ResolveAttachmentTarget(1, true), SceneRenderTarget::HdrComposited);
    EXPECT_EQ(profile.ResolveAttachmentTarget(2, true), SceneRenderTarget::Count);
    EXPECT_EQ(profile.ResolveAttachmentTarget(UINT32_MAX, true), SceneRenderTarget::Count);
    profile.CompositedHdrAttachment = false;
    EXPECT_EQ(profile.ResolveAttachmentTarget(1, true), SceneRenderTarget::HdrColor);
    profile = {.AttachmentCount = 2, .AttachmentTargets = {SceneRenderTarget::ObjectDepth}};
    EXPECT_EQ(profile.ResolveAttachmentTarget(0, false), SceneRenderTarget::ObjectDepth);
    EXPECT_EQ(profile.ResolveAttachmentTarget(1, false), SceneRenderTarget::Count); // Partial recipes must not default to Depth.
}

TEST(RenderGraphScenePipelineTest, CanonicalRenderPassCompatibilityIgnoresLoadsButChecksFormatsAndDependencies)
{
    MemoryManager memory = {};
    memory.Initialize(ZKilo(32), {});
    auto device                                   = std::make_unique<Hardwares::VulkanDevice>();
    device->PhysicalDeviceSupportDynamicRendering = true; // No Vulkan objects are needed for this check.
    const auto specification                      = [&](Rendering::Specifications::ImageFormat format, bool dependency) {
        Rendering::Specifications::AttachmentSpecification result = {};
        result.ColorsMap.init(&memory.MainArena, 1);
        result.ColorsMap[0] = {.Format = format};
        if (dependency)
        {
            result.DependenciesMap.init(&memory.MainArena, 1);
            result.DependenciesMap[0] = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0, .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        }
        return result;
    };
    const auto format           = Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    auto       loaded           = specification(format, false);
    loaded.ColorsMap[0].Load    = Rendering::Specifications::LoadOperation::LOAD;
    loaded.ColorsMap[0].Initial = Rendering::Specifications::ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
    RenderPasses::Attachment prepared(device.get(), specification(format, false));
    RenderPasses::Attachment active(device.get(), std::move(loaded));
    EXPECT_TRUE(prepared.IsRenderPassCompatibleWith(active));
    RenderPasses::Attachment other_format(device.get(), specification(Rendering::Specifications::ImageFormat::R32_UINT, false));
    EXPECT_FALSE(prepared.IsRenderPassCompatibleWith(other_format));
    RenderPasses::Attachment dependency(device.get(), specification(format, true));
    EXPECT_FALSE(prepared.IsRenderPassCompatibleWith(dependency));
    RenderPasses::Attachment same_dependency(device.get(), specification(format, true));
    EXPECT_TRUE(dependency.IsRenderPassCompatibleWith(same_dependency));
    auto different_mask                               = specification(format, true);
    different_mask.DependenciesMap[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    RenderPasses::Attachment different_dependency(device.get(), std::move(different_mask));
    EXPECT_FALSE(dependency.IsRenderPassCompatibleWith(different_dependency));
}

TEST(RenderGraphScenePipelineTest, CheckedAttachmentAdmissionRejectsMalformedRecipesWithoutNativeCalls)
{
    MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    auto device                                   = std::make_unique<Hardwares::VulkanDevice>();
    device->PhysicalDeviceSupportDynamicRendering = true;
    for (uint32_t invalid = 0; invalid < 16; ++invalid)
    {
        SCOPED_TRACE(invalid);
        AttachmentSpecification specification = {};
        specification.ColorsMap.init(&memory.MainArena, 32);
        specification.DependenciesMap.init(&memory.MainArena, 128);
        specification.ColorsMap[0] = {.Format = ImageFormat::R8G8B8A8_UNORM};
        auto expected              = VK_ERROR_INITIALIZATION_FAILED;
        switch (invalid)
        {
            case 0:
                specification.ColorsMap.clear();
                break;
            case 1:
                specification.BindPoint = PipelineBindPoint::COMPUTE;
                break;
            case 2:
                specification.ColorsMap[0].Format = static_cast<ImageFormat>(UINT32_MAX);
                expected                          = VK_ERROR_FORMAT_NOT_SUPPORTED;
                break;
            case 3:
                specification.ColorsMap[0].Load = static_cast<LoadOperation>(UINT32_MAX);
                break;
            case 4:
                specification.ColorsMap[0].Store = static_cast<StoreOperation>(UINT32_MAX);
                break;
            case 5:
                specification.ColorsMap[0].Initial = static_cast<ImageLayout>(UINT32_MAX);
                break;
            case 6:
                specification.ColorsMap[0].Final = static_cast<ImageLayout>(UINT32_MAX);
                break;
            case 7:
                specification.ColorsMap[0].ReferenceLayout = static_cast<ImageLayout>(UINT32_MAX);
                break;
            case 8:
                specification.ColorsMap.clear();
                specification.ColorsMap[1] = {.Format = ImageFormat::R8G8B8A8_UNORM};
                break;
            case 9:
                specification.ColorsMap[0].Format = ImageFormat::DEPTH16_UNORM;
                specification.ColorsMap[1].Format = ImageFormat::DEPTH16_UNORM;
                break;
            case 10:
            case 11:
                for (uint32_t i = 1; i < (invalid == 10 ? 9u : 10u); ++i)
                {
                    specification.ColorsMap[i].Format = ImageFormat::R8G8B8A8_UNORM;
                }
                break;
            case 12:
                specification.DependenciesMap[1] = {};
                break;
            case 13:
                specification.DependenciesMap[0] = {.srcSubpass = 1};
                break;
            case 14:
                specification.DependenciesMap[0] = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = VK_SUBPASS_EXTERNAL};
                break;
            case 15:
                for (uint32_t i = 0; i <= RenderPasses::Attachment::MaxDependencies; ++i)
                {
                    specification.DependenciesMap[i] = {};
                }
                break;
        }
        VkResult                 status = VK_SUCCESS;
        RenderPasses::Attachment attachment(device.get(), std::move(specification), &status);
        EXPECT_EQ(status, expected);
        EXPECT_EQ(attachment.GetHandle(), VK_NULL_HANDLE);
        EXPECT_EQ(attachment.GetColorAttachmentCount(), 0u);
        EXPECT_FALSE(attachment.TryPrepare());
    }
}

TEST(RenderGraphScenePipelineTest, AdmittedGraphicsDescriptionCloneChecksBothArraysAndPreservesOutputOnFailure)
{
    MemoryManager memory = {};
    memory.Initialize(ZKilo(32), {});
    Rendering::Specifications::GraphicsPipelineDesc source = {};
    source.DebugName                                       = "admitted";
    source.EnableBlending                                  = true;
    source.EnableDepthWrite                                = false;
    source.VertexInputBindingSpecifications.init(&memory.MainArena, 2, 2);
    source.VertexInputBindingSpecifications[0].Stride = 32;
    source.VertexInputAttributeSpecifications.init(&memory.MainArena, 1, 1);
    source.VertexInputAttributeSpecifications[0].Location  = 3;
    Rendering::Specifications::GraphicsPipelineDesc output = {};
    output.DebugName                                       = "preserved";
    std::array<std::max_align_t, 32> storage               = {};
    for (const auto bytes : {size_t(1), 2 * sizeof(Rendering::Specifications::VertexInputBindingSpecification)})
    {
        ArenaAllocator bounded = {};
        ASSERT_TRUE(bounded.InitializeBorrowed(storage.data(), bytes));
        EXPECT_FALSE(Rendering::Specifications::TryCloneGraphicsPipelineDesc(source, &bounded, &output));
        EXPECT_EQ(bounded.m_current_offset, 0u);
        EXPECT_STREQ(output.DebugName, "preserved");
        EXPECT_TRUE(output.VertexInputBindingSpecifications.empty());
    }
    ASSERT_TRUE(Rendering::Specifications::TryCloneGraphicsPipelineDesc(source, &memory.MainArena, &output));
    EXPECT_STREQ(output.DebugName, "admitted");
    EXPECT_TRUE(output.EnableBlending);
    EXPECT_FALSE(output.EnableDepthWrite);
    ASSERT_EQ(output.VertexInputBindingSpecifications.size(), 2u);
    ASSERT_EQ(output.VertexInputAttributeSpecifications.size(), 1u);
    source.VertexInputBindingSpecifications[0].Stride = 99;
    EXPECT_EQ(output.VertexInputBindingSpecifications[0].Stride, 32u);
    EXPECT_EQ(output.VertexInputAttributeSpecifications[0].Location, 3u);
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct DescriptorReplayTestAccess
    {
        static void Record(DescriptorBoundPass& pass, const char* name)
        {
            pass.RecordDescriptorBinding({.Binding = {.Name = name}, .FrameIndex = 0});
        }
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses

TEST(RenderGraphScenePipelineTest, CheckedBuiltinDescriptionsPreserveOutputAndRecipeCursorAtEachReservationBoundary)
{
    GizmoPass                 gizmo              = {};
    ZUIPass                   ui                 = {};
    IRenderGraphCallbackPass* passes[]           = {&gizmo, &ui};
    const size_t              attribute_counts[] = {6, 3};
    for (uint32_t i = 0; i < 2; ++i)
    {
        SCOPED_TRACE(i);
        Rendering::Specifications::GraphicsPipelineDesc output                    = {};
        output.DebugName                                                          = "outgoing";
        output.EnableStencilTest                                                  = true;
        const auto                                                  binding_bytes = sizeof(Rendering::Specifications::VertexInputBindingSpecification);
        const auto                                                  required      = Helpers::memory_align_size_t(binding_bytes, ZAlignof<Rendering::Specifications::VertexInputAttributeSpecification>()) + attribute_counts[i] * sizeof(Rendering::Specifications::VertexInputAttributeSpecification);
        alignas(DEFAULT_ALIGNMENT) std::array<std::max_align_t, 64> storage       = {};
        for (const auto bytes : {size_t(1), binding_bytes, required - 1})
        {
            ArenaAllocator arena = {};
            ASSERT_TRUE(arena.InitializeBorrowed(storage.data(), bytes));
            EXPECT_FALSE(passes[i]->TryBuildGraphicsPipelineDescription(&arena, &output));
            EXPECT_EQ(arena.m_current_offset, 0u);
            EXPECT_STREQ(output.DebugName, "outgoing");
            EXPECT_TRUE(output.EnableStencilTest);
            EXPECT_TRUE(output.VertexInputBindingSpecifications.empty());
            EXPECT_TRUE(output.VertexInputAttributeSpecifications.empty());
        }
        EXPECT_FALSE(passes[i]->TryBuildGraphicsPipelineDescription(nullptr, &output));
        ArenaAllocator exact = {};
        ASSERT_TRUE(exact.InitializeBorrowed(storage.data(), required));
        EXPECT_FALSE(passes[i]->TryBuildGraphicsPipelineDescription(&exact, nullptr));
        EXPECT_EQ(exact.m_current_offset, 0u);
        ASSERT_TRUE(passes[i]->TryBuildGraphicsPipelineDescription(&exact, &output));
        ASSERT_EQ(output.VertexInputBindingSpecifications.size(), 1u);
        EXPECT_EQ(output.VertexInputAttributeSpecifications.size(), attribute_counts[i]);
        EXPECT_EQ(exact.m_current_offset, required);
        EXPECT_FALSE(output.EnableStencilTest);
        EXPECT_NE(output.ShaderSpecificationValue.Name, nullptr);
    }
}

TEST(RenderGraphScenePipelineTest, UnqualifiedDescriptionCallbackFailsClosedAndStaticRecipeNeedsNoArena)
{
    struct Unqualified : IRenderGraphCallbackPass
    {
        bool Register(Hardwares::VulkanDevice*, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilder*, RenderGraphResourceInspector*) override
        {
            return false;
        }
        void Execute(Hardwares::VulkanDevice*, RenderGraphResourceInspector*, Rendering::Scenes::SceneData*, RenderPasses::RenderPass*, Rendering::Buffers::FramebufferVNext*, Hardwares::CommandBuffer*) override {}
    } unqualified                                          = {};
    Rendering::Specifications::GraphicsPipelineDesc output = {};
    output.DebugName                                       = "preserved";
    EXPECT_FALSE(unqualified.TryBuildGraphicsPipelineDescription(nullptr, &output));
    EXPECT_STREQ(output.DebugName, "preserved");
    GridPass grid = {};
    EXPECT_FALSE(grid.TryBuildGraphicsPipelineDescription(nullptr, nullptr));
    ASSERT_TRUE(grid.TryBuildGraphicsPipelineDescription(nullptr, &output));
    EXPECT_STREQ(output.ShaderSpecificationValue.Name, "infinite_grid");
    EXPECT_TRUE(output.VertexInputBindingSpecifications.empty());
    EXPECT_TRUE(output.VertexInputAttributeSpecifications.empty());
}

TEST(RenderGraphScenePipelineTest, DescriptorReplayNamesHaveExactCapsAndResetWithoutArenaGrowth)
{
    struct Names final : RenderPasses::DescriptorBoundPass
    {
        void Initialize(Hardwares::VulkanDevice*, Rendering::Specifications::RenderPassSpecification) override {}
        void Dispose() override {}
        void Bake() override {}
        bool Verify() override
        {
            return DescriptorReplayValid;
        }
        void Record(const char* name)
        {
            RenderPasses::DescriptorReplayTestAccess::Record(*this, name);
        }
        uint32_t Bytes() const
        {
            return DescriptorReplayNameBytes;
        }
        uint32_t Count() const
        {
            return DescriptorReplayRecordCount;
        }
        const char* First() const
        {
            return DescriptorReplayRecords[0].Name;
        }
        Pipelines::IPipeline* GetPipeline() const override
        {
            return nullptr;
        }
    } names;
    // No device or arena exists: replay names must be entirely pass-owned.
    std::array<char, RenderPasses::kMaxDescriptorReplayNameBytes> maximum = {};
    maximum.fill('a');
    maximum.back() = '\0';
    names.Record(maximum.data());
    ASSERT_TRUE(names.Verify());
    EXPECT_EQ(names.Bytes(), maximum.size());
    maximum[0] = 'z';
    EXPECT_EQ(names.First()[0], 'a');
    maximum[0] = 'a';
    names.Record(maximum.data());
    EXPECT_TRUE(names.Verify());
    EXPECT_EQ(names.Bytes(), maximum.size());
    names.Record("extra");
    EXPECT_FALSE(names.Verify());
    EXPECT_EQ(names.Count(), 1u);
    names.ResetSceneBindings();
    EXPECT_TRUE(names.Verify());
    EXPECT_EQ(names.Bytes(), 0u);
    for (uint32_t index = 0; index < RenderPasses::kMaxDescriptorReplayBindings; ++index)
    {
        char name[32] = {};
        std::snprintf(name, sizeof(name), "binding%u", index);
        names.Record(name);
    }
    EXPECT_TRUE(names.Verify());
    EXPECT_EQ(names.Count(), RenderPasses::kMaxDescriptorReplayBindings);
    names.Record("too-many-records");
    EXPECT_FALSE(names.Verify());
    names.ResetSceneBindings();
    names.Record("retry");
    EXPECT_TRUE(names.Verify());
    EXPECT_STREQ(names.First(), "retry");
}

namespace ZEngine::Rendering::Renderers
{
    struct RenderGraphTestAccess
    {
        static void AddReadbackPasses(RenderGraph& graph)
        {
            graph.AddReadbackPasses();
        }
        static void CancelUnsubmittedReadbacks(RenderGraph& graph, bool preserve_recorded)
        {
            graph.CancelUnsubmittedReadbacks(preserve_recorded);
        }
        static void BuildStreamingAcquirePlans(RenderGraph& graph)
        {
            graph.BuildStreamingAcquirePlans();
        }
        static void BuildQueueSchedule(RenderGraph& graph)
        {
            graph.BuildQueueSchedule();
        }
    };
} // namespace ZEngine::Rendering::Renderers

TEST(RenderGraphBarrierTest, DiscardedTransientReuseWaitsForPreviousFrameReadback)
{
    const RGImageBarrierPlan plan = {
        .DestinationState = GetRGAccessState(RGAccess::ColorWrite),
        .DiscardContents  = true,
        .Range            = {.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .LevelCount = 1, .LayerCount = 1},
    };
    // Registration resets logical history even though the pooled image was
    // read by vkCmdCopyImageToBuffer in the previous frame.
    const auto barrier = BuildRGImageBarrier(plan, {}, VK_NULL_HANDLE);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_NE(barrier.srcStageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0u);
    EXPECT_NE(barrier.srcAccessMask & VK_ACCESS_2_MEMORY_WRITE_BIT, 0u);
    EXPECT_EQ(barrier.dstStageMask, plan.DestinationState.Stage);
    EXPECT_EQ(barrier.dstAccessMask, plan.DestinationState.Access);
    EXPECT_EQ(barrier.newLayout, plan.DestinationState.Layout);
    EXPECT_EQ(barrier.subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
}

TEST(RenderGraphBarrierTest, DiscardedAliasedDepthWaitsForEarlierColorImageCopy)
{
    const RGImageBarrierPlan plan = {
        .DestinationState = GetRGAccessState(RGAccess::DepthWrite),
        .DiscardContents  = true,
        .Range            = {.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .LevelCount = 1, .LayerCount = 1},
    };
    const auto barrier = BuildRGImageBarrier(plan, GetRGAccessState(RGAccess::TransferRead), VK_NULL_HANDLE);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(barrier.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(barrier.newLayout, plan.DestinationState.Layout);
    EXPECT_EQ(barrier.subresourceRange.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT);
}

TEST(RenderGraphBarrierTest, PreservedImageUsesItsTrackedSourceScopeAndLayout)
{
    const auto               source = GetRGAccessState(RGAccess::TransferRead);
    const RGImageBarrierPlan plan   = {
        .DestinationState = GetRGAccessState(RGAccess::ShaderRead),
        .Range            = {.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .BaseMipLevel = 2, .LevelCount = 1, .BaseArrayLayer = 3, .LayerCount = 1},
    };
    const auto barrier = BuildRGImageBarrier(plan, source, VK_NULL_HANDLE);
    EXPECT_EQ(barrier.srcStageMask, source.Stage);
    EXPECT_EQ(barrier.srcAccessMask, source.Access);
    EXPECT_EQ(barrier.oldLayout, source.Layout);
    EXPECT_EQ(barrier.newLayout, plan.DestinationState.Layout);
    EXPECT_EQ(barrier.subresourceRange.baseMipLevel, 2u);
    EXPECT_EQ(barrier.subresourceRange.baseArrayLayer, 3u);
}

namespace
{
    TEST(SelectionOutlineFrameTest, SortAndUniqueKeepsCompleteEntityIdentity)
    {
        SelectionOutlineFrame frame = {};
        ASSERT_TRUE(frame.Add({.Index = 4, .Generation = 2}));
        ASSERT_TRUE(frame.Add({.Index = 1, .Generation = 3}));
        ASSERT_TRUE(frame.Add({.Index = 4, .Generation = 2}));
        ASSERT_TRUE(frame.Add({.Index = 4, .Generation = 1}));

        frame.SortAndUnique();

        ASSERT_EQ(frame.EntityCount, 3u);
        EXPECT_TRUE(frame.Contains({.Index = 1, .Generation = 3}));
        EXPECT_TRUE(frame.Contains({.Index = 4, .Generation = 1}));
        EXPECT_TRUE(frame.Contains({.Index = 4, .Generation = 2}));
        EXPECT_FALSE(frame.Contains({.Index = 4, .Generation = 3}));
        EXPECT_FALSE(frame.Contains({.Index = 1, .Generation = 0}));
    }

    RGPass MakePass(ArenaAllocator* arena, cstring name)
    {
        RGPass pass;
        pass.Name = name;
        pass.Reads.init(arena, 4);
        pass.Writes.init(arena, 4);
        pass.QueryWrites.init(arena, 2);
        pass.QueryResets.init(arena, 2);
        return pass;
    }

    void AddRead(RGPass& pass, uint32_t resource_index, uint32_t version = 0)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, version};
        pass.Reads.push(pr);
    }

    void AddWrite(RGPass& pass, uint32_t resource_index, uint32_t version = 0)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, version};
        pass.Writes.push(pr);
    }

    Array<RGResource> MakeResources(ArenaAllocator* arena, uint32_t count)
    {
        Array<RGResource> resources;
        resources.init(arena, count, count);
        return resources;
    }

    void CountReadbackCancellation(void* context)
    {
        ++*static_cast<uint32_t*>(context);
    }

    struct TestTransferPass final : ITransferPass
    {
        void RegisterTransfer(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const /*res_builder*/) override {}
        void RecordTransfer(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct TestComputePass final : IInlineComputePass
    {
        cstring GetShaderName() const override
        {
            return "test_compute";
        }
        uint32_t GetPushConstantSize() const override
        {
            return 16;
        }
        void RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const /*res_builder*/) override {}
        void ExecuteCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct FailedRegistrationPass final : IRenderGraphCallbackPass
    {
        RGResourceHandle Produced = {};

        bool             Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const builder, RenderGraphResourceInspectorPtr /*inspector*/) override
        {
            Produced = builder->WriteBuffer("SharedBuffer", 64);
            builder->Export(Produced);
            return false;
        }

        void Execute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const /*pass*/, Rendering::Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct ValidRegistrationPass final : IRenderGraphCallbackPass
    {
        RGResourceHandle Produced = {};
        RGResourceHandle Consumed = {};

        bool             Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const builder, RenderGraphResourceInspectorPtr /*inspector*/) override
        {
            Produced = builder->WriteBuffer("SharedBuffer", 64);
            Consumed = builder->ReadBuffer(Produced);
            builder->Export(Produced);
            return true;
        }

        void Execute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const /*pass*/, Rendering::Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct ToneMappingOutputPass final : IRenderGraphCallbackPass
    {
        bool Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const builder, RenderGraphResourceInspectorPtr /*inspector*/) override
        {
            builder->WriteColorAttachment(
                RendererResourceName::FrameColorRenderTargetName,
                {
                .IsUsageSampled = true,
                .Width          = 1,
                .Height         = 1,
                .Format         = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM,
                .LoadOp         = ZEngine::Rendering::Specifications::LoadOperation::CLEAR,
                });
            return true;
        }

        void Execute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const /*pass*/, Rendering::Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct DisplayOverlayPass final : IRenderGraphCallbackPass
    {
        bool Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const builder, RenderGraphResourceInspectorPtr /*inspector*/) override
        {
            builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = ZEngine::Rendering::Specifications::LoadOperation::LOAD});
            return true;
        }

        void Execute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const /*pass*/, Rendering::Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    void TestReadbackCallback(const void* /*data*/, size_t /*size*/, void* /*context*/) {}
    void TestReadbackCancellation(void* /*context*/) {}
} // namespace

TEST(TextureSpecificationTest, DefaultsDescribeAnEmpty2DTexture)
{
    ZEngine::Rendering::Specifications::TextureSpecification specification;

    EXPECT_TRUE(specification.IsUsageSampled);
    EXPECT_FALSE(specification.IsUsageStorage);
    EXPECT_TRUE(specification.IsUsageTransfert);
    EXPECT_FALSE(specification.IsUsageTransferSource);
    EXPECT_FALSE(specification.IsAliasable);
    EXPECT_FALSE(specification.IsCubemap);
    EXPECT_FALSE(specification.Is3D);
    EXPECT_FALSE(specification.IsRenderTargetSized);
    EXPECT_EQ(specification.Width, 0U);
    EXPECT_EQ(specification.Height, 0U);
    EXPECT_EQ(specification.Depth, 1U);
    EXPECT_EQ(specification.BytePerPixel, 4U);
    EXPECT_EQ(specification.MipLevelCount, 1U);
    EXPECT_EQ(specification.LayerCount, 1U);
    EXPECT_EQ(specification.Format, ZEngine::Rendering::Specifications::ImageFormat::UNDEFINED);
    EXPECT_EQ(specification.LoadOp, ZEngine::Rendering::Specifications::LoadOperation::CLEAR);
    EXPECT_FLOAT_EQ(specification.ClearColor[0], 0.0f);
    EXPECT_FLOAT_EQ(specification.ClearColor[1], 0.0f);
    EXPECT_FLOAT_EQ(specification.ClearColor[2], 0.0f);
    EXPECT_FLOAT_EQ(specification.ClearColor[3], 0.0f);
    EXPECT_FLOAT_EQ(specification.ClearDepth, 1.0f);
    EXPECT_EQ(specification.ClearStencil, 0U);
}

TEST(ImageBufferSpecificationTest, DefaultsDescribeAnEmpty2DColorImage)
{
    ZEngine::Rendering::Specifications::ImageBufferSpecification specification;

    EXPECT_EQ(specification.Width, 0U);
    EXPECT_EQ(specification.Height, 0U);
    EXPECT_EQ(specification.Depth, 1U);
    EXPECT_EQ(specification.ImageViewTypeValue, ZEngine::Rendering::Specifications::ImageViewType::TYPE_2D);
    EXPECT_EQ(specification.BufferUsageType, ZEngine::Rendering::Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE);
    EXPECT_EQ(specification.ImageFormat, VK_FORMAT_UNDEFINED);
    EXPECT_EQ(specification.ImageUsage, 0U);
    EXPECT_EQ(specification.ImageAspectFlag, VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(specification.MipLevelCount, 1U);
    EXPECT_EQ(specification.LayerCount, 1U);
    EXPECT_EQ(specification.ImageCreateFlagValue, ZEngine::Rendering::Specifications::ImageCreateFlag::NONE);
    EXPECT_FALSE(specification.IsAliasable);
}

TEST(RenderGraphTransientPoolTest, RequiresUsageSupersetForAliasing)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification base{};
    base.Width  = 128;
    base.Height = 128;
    base.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({1, 0}, base, 0);

    auto storage           = base;
    storage.IsUsageStorage = true;
    EXPECT_FALSE(pool.TryAlias(storage, 1).Valid());

    auto transfer_source                  = base;
    transfer_source.IsUsageTransferSource = true;
    EXPECT_FALSE(pool.TryAlias(transfer_source, 1).Valid());

    auto volume  = base;
    volume.Is3D  = true;
    volume.Depth = 32;
    EXPECT_FALSE(pool.TryAlias(volume, 1).Valid());

    EXPECT_TRUE(pool.TryAlias(base, 1).Valid());
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, RequiresIdenticalVolumeDepthForAliasing)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification volume{};
    volume.Is3D   = true;
    volume.Width  = 32;
    volume.Height = 32;
    volume.Depth  = 32;
    volume.Format = ZEngine::Rendering::Specifications::ImageFormat::R16G16B16A16_SFLOAT;
    pool.Register({1, 0}, volume, 0);

    auto different_depth  = volume;
    different_depth.Depth = 16;
    EXPECT_FALSE(pool.TryAlias(different_depth, 1).Valid());
    EXPECT_TRUE(pool.TryAlias(volume, 1).Valid());
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, DoesNotAliasFixedAndRenderTargetSizedImages)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification render_target{};
    render_target.IsRenderTargetSized = true;
    render_target.Width               = 128;
    render_target.Height              = 128;
    render_target.Format              = ZEngine::Rendering::Specifications::ImageFormat::R16G16B16A16_SFLOAT;
    pool.Register({1, 0}, render_target, 0);

    auto fixed_size                = render_target;
    fixed_size.IsRenderTargetSized = false;
    EXPECT_FALSE(pool.TryAlias(fixed_size, 1).Valid());
    EXPECT_TRUE(pool.TryAlias(render_target, 1).Valid());
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesOnlyCompatibleBufferStorage)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView buffer = {};
    buffer.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&buffer, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0);

    EXPECT_EQ(pool.TryAlias(512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1), &buffer);
    EXPECT_EQ(pool.TryAlias(1024, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, 1), nullptr);
    EXPECT_EQ(pool.TryAlias(2048, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1), nullptr);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesBackingAllocationOnlyForNonOverlappingImageObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification spec{};
    spec.Width  = 128;
    spec.Height = 128;
    spec.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({1, 0}, spec, 2);
    pool.RegisterAlias(&pool.Slots[0], "A", {1, 0}, spec, 0, 2);

    pool.BeginFrame();
    EXPECT_TRUE(pool.FindNamedAlias("A", spec, 0, 2).Valid());
    EXPECT_EQ(pool.FindAliasingSlot(spec, 1, 3), nullptr);
    EXPECT_EQ(pool.FindAliasingSlot(spec, 3, 4), &pool.Slots[0]);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesBackingAllocationOnlyForNonOverlappingBufferObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView backing = {};
    backing.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
    pool.RegisterAlias(&pool.Slots[0], "A", &backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2);

    pool.BeginFrame();
    EXPECT_EQ(pool.FindNamedAlias("A", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2), &backing);
    EXPECT_EQ(pool.FindAliasingSlot(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, 3), nullptr);
    EXPECT_EQ(pool.FindAliasingSlot(512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 3, 4), &pool.Slots[0]);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, RetainsNamedAliasingObjectsAcrossFrames)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);
    ZEngine::Rendering::Specifications::TextureSpecification spec{};
    spec.Width  = 64;
    spec.Height = 64;
    spec.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({7, 3}, spec, 1);
    pool.RegisterAlias(&pool.Slots[0], "History", {7, 3}, spec, 0, 1);

    pool.BeginFrame();
    const auto first = pool.FindNamedAlias("History", spec, 0, 1);
    ASSERT_TRUE(first.Valid());

    pool.BeginFrame();
    const auto second = pool.FindNamedAlias("History", spec, 0, 1);
    EXPECT_EQ(second.Index, first.Index);
    EXPECT_EQ(second.Generation, first.Generation);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, RejectsOverlappingNamedAliasingObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView backing = {};
    backing.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
    pool.RegisterAlias(&pool.Slots[0], "Scratch", &backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2);

    pool.BeginFrame();
    EXPECT_EQ(pool.FindNamedAlias("Scratch", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2), &backing);
    EXPECT_EQ(pool.FindNamedAlias("Scratch", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, 3), nullptr);
    manager.Shutdown();
}

TEST(RenderGraphTransientStatisticsTest, CalculatesNonNegativeAliasingSavings)
{
    RGTransientStatistics statistics = {};
    statistics.VirtualImageBytes     = 1024;
    statistics.VirtualBufferBytes    = 512;
    statistics.PhysicalImageBytes    = 768;
    statistics.PhysicalBufferBytes   = 256;
    EXPECT_EQ(statistics.EstimatedAliasingSavings(), 512u);

    statistics.PhysicalImageBytes = 2048;
    EXPECT_EQ(statistics.EstimatedAliasingSavings(), 0u);
}

TEST(RenderGraphTimestampTest, ConvertsWrappedTimestampPairsToMilliseconds)
{
    // An eight-bit GPU timestamp wraps from 255 to zero. 250 -> 5 is 11 ticks.
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(250, 5, 8, 1000000.0f), 11.0f);
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(10, 15, 64, 200000.0f), 1.0f);
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(10, 15, 0, 1000000.0f), 0.0f);
}

TEST(RenderGraphDebugDumpTest, ProducesDotAndJsonWithoutStdContainers)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Resources.init(&arena, 1);
    graph.Resources.push({.Name = "FrameColor", .Kind = RGResourceKind::Attachment, .FirstPassIndex = 0, .LastPassIndex = 0});
    graph.Passes.init(&arena, 1);
    RGPass pass = MakePass(&arena, "Composite");
    pass.Queue  = ZEngine::Rendering::QueueType::GRAPHIC_QUEUE;
    AddRead(pass, 0);
    AddWrite(pass, 0, 1);
    graph.Passes.push(std::move(pass));
    graph.SortedPassIndices.init(&arena, 1);
    graph.SortedPassIndices.push(0);
    graph.QueueBatches.init(&arena, 1);
    graph.QueueBatches.push({.Queue = ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, .FirstPassOrder = 0, .PassCount = 1});
    graph.TransientStatistics.VirtualImageBytes  = 1024;
    graph.TransientStatistics.PhysicalImageBytes = 512;

    String dump;
    dump.init(&arena, 512);
    graph.WriteDebugDump(dump, RGDebugDumpFormat::Dot);
    EXPECT_NE(std::strstr(dump.c_str(), "digraph RenderGraph"), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "FrameColor"), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "Composite"), nullptr);

    graph.WriteDebugDump(dump, RGDebugDumpFormat::Json);
    EXPECT_NE(std::strstr(dump.c_str(), "\"statistics\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"resources\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"batches\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"timings\""), nullptr);
    manager.Shutdown();
}

TEST(GridPassTest, BuildsStaticPipelineDescription)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&      arena = manager.MainArena;

    GridPass   pass  = {};
    const auto desc  = pass.BuildGraphicsPipelineDescription(&arena);

    EXPECT_STREQ(desc.DebugName, "Infinite-Grid-Pipeline");
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "infinite_grid");
    EXPECT_FALSE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_TRUE(desc.EnableBlending);
    EXPECT_TRUE(desc.VertexInputBindingSpecifications.empty());
    EXPECT_TRUE(desc.VertexInputAttributeSpecifications.empty());

    manager.Shutdown();
}

TEST(RenderGraphCallbackPassTest, DescribesComputePipelineWithoutGraphicsPassBuilder)
{
    TestComputePass pass = {};

    EXPECT_EQ(pass.GetPipelineType(), ZEngine::Rendering::Specifications::RenderPassType::COMPUTE);
    EXPECT_STREQ(pass.GetComputeShaderName(), "test_compute");
    EXPECT_EQ(pass.GetComputePushConstantSize(), 16u);
}

TEST(RenderGraphDeclarationValidationTest, RejectsReadWithoutProducer)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "Reader");
    AddRead(reader, 0, 1);
    passes.push(std::move(reader));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::MissingProducer);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, AcceptsImportedInitialVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "Reader");
    AddRead(reader, 0);
    passes.push(std::move(reader));
    auto resources             = MakeResources(&arena, 1);
    resources[0].External      = true;
    resources[0].TextureHandle = {1, 0};

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, AcceptsImportedBufferInitialVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "BufferReader");
    AddRead(reader, 0);
    passes.push(std::move(reader));

    Core::Memory::BufferView buffer = {};
    buffer.Handle                   = reinterpret_cast<VkBuffer>(1);
    auto resources                  = MakeResources(&arena, 1);
    resources[0].Kind               = RGResourceKind::Buffer;
    resources[0].External           = true;
    resources[0].Buffer             = &buffer;

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, OrdersFrameColorBeforeSwapchainWriter)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass scene = MakePass(&arena, "Scene");
    AddWrite(scene, 0, 1);
    passes.push(std::move(scene));

    RGPass ui = MakePass(&arena, "ZUI");
    AddRead(ui, 0, 1);
    AddWrite(ui, 1, 1);
    passes.push(std::move(ui));

    auto resources             = MakeResources(&arena, 2);
    resources[0].TextureHandle = {1, 0};
    resources[1].Kind          = RGResourceKind::Swapchain;
    resources[1].External      = true;

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));

    Array<uint32_t> order;
    order.init(&arena, 2);
    uint32_t cycle_pass = UINT32_MAX;
    EXPECT_TRUE(BuildPassTopology(&arena, passes, order, &cycle_pass));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    manager.Shutdown();
}

TEST(RenderGraphImportedBufferTest, RebindsTheActiveFrameBufferOnly)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                    arena       = manager.MainArena;

    Core::Memory::BufferView frame_zero  = {};
    frame_zero.Handle                    = reinterpret_cast<VkBuffer>(1);
    Core::Memory::BufferView frame_one   = {};
    frame_one.Handle                     = reinterpret_cast<VkBuffer>(2);
    Core::Memory::BufferView null_buffer = {};

    RenderGraph              graph;
    graph.Resources.init(&arena, 2);
    graph.ResourceIndex.init(&arena, 2);
    auto& resource_zero                        = graph.Resources.push_use({});
    resource_zero.Name                         = "CullingInput_Frame0";
    resource_zero.Kind                         = RGResourceKind::Buffer;
    resource_zero.External                     = true;
    resource_zero.Buffer                       = &frame_zero;
    auto& resource_one                         = graph.Resources.push_use({});
    resource_one.Name                          = "CullingInput_Frame1";
    resource_one.Kind                          = RGResourceKind::Buffer;
    resource_one.External                      = true;
    resource_one.Buffer                        = &frame_zero;
    graph.ResourceIndex["CullingInput_Frame0"] = 0;
    graph.ResourceIndex["CullingInput_Frame1"] = 1;

    EXPECT_TRUE(graph.UpdateImportedBuffer("CullingInput_Frame1", &frame_one));
    EXPECT_EQ(graph.Resources[0].Buffer, &frame_zero);
    EXPECT_EQ(graph.Resources[1].Buffer, &frame_one);
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame2", &frame_one));
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame1", nullptr));
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame1", &null_buffer));
    EXPECT_EQ(graph.Resources[1].Buffer, &frame_one);
    manager.Shutdown();
}

TEST(RenderGraphSynchronizationTest, IndirectReadUsesDrawIndirectAccess)
{
    const RGResourceState state = GetRGAccessState(RGAccess::IndirectRead);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_UNDEFINED);
}

TEST(RenderGraphSynchronizationTest, ShaderReadUsesPassPipelineStages)
{
    const RGResourceState graphics_state = GetRGAccessState(RGAccess::ShaderRead, ZEngine::Rendering::Specifications::RenderPassType::GRAPHIC);
    const RGResourceState compute_state  = GetRGAccessState(RGAccess::ShaderRead, ZEngine::Rendering::Specifications::RenderPassType::COMPUTE);

    EXPECT_EQ(graphics_state.Stage, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    EXPECT_EQ(compute_state.Stage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(graphics_state.Access, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(graphics_state.Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TEST(RenderGraphSynchronizationTest, ShaderReadWriteUsesPassPipelineStages)
{
    const RGResourceState graphics_state = GetRGAccessState(RGAccess::ShaderReadWrite, ZEngine::Rendering::Specifications::RenderPassType::GRAPHIC);
    const RGResourceState compute_state  = GetRGAccessState(RGAccess::ShaderReadWrite, ZEngine::Rendering::Specifications::RenderPassType::COMPUTE);

    EXPECT_EQ(graphics_state.Stage, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    EXPECT_EQ(compute_state.Stage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(graphics_state.Access, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    EXPECT_EQ(graphics_state.Layout, VK_IMAGE_LAYOUT_GENERAL);
}

TEST(RenderGraphSynchronizationTest, StorageWriteUsesPassPipelineStages)
{
    const RGResourceState graphics_state = GetRGAccessState(RGAccess::StorageWrite, ZEngine::Rendering::Specifications::RenderPassType::GRAPHIC);
    const RGResourceState compute_state  = GetRGAccessState(RGAccess::StorageWrite, ZEngine::Rendering::Specifications::RenderPassType::COMPUTE);

    EXPECT_EQ(graphics_state.Stage, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    EXPECT_EQ(compute_state.Stage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(graphics_state.Access, VK_ACCESS_2_SHADER_WRITE_BIT);
    EXPECT_EQ(graphics_state.Layout, VK_IMAGE_LAYOUT_GENERAL);
}

TEST(RenderGraphSynchronizationTest, ColorAttachmentLoadUsesReadWriteState)
{
    const RGResourceState state = GetRGAccessState(RGAccess::ColorReadWrite);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(RenderGraphSynchronizationTest, DepthReadPreservesStoredAttachmentState)
{
    const RGResourceState state = GetRGAccessState(RGAccess::DepthRead);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

TEST(RenderGraphSynchronizationTest, StreamingOwnershipAcquireRepeatsProducerLayoutPair)
{
    Hardwares::StreamingUploadTicket ticket = {};
    ticket.PreReleaseLayout                 = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ticket.PostReleaseLayout                = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ticket.ProducerQueueFamily              = 3;
    const auto               destination    = GetRGAccessState(RGAccess::ShaderRead);
    const RGSubresourceRange range          = {.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .BaseMipLevel = 0, .LevelCount = 4, .BaseArrayLayer = 0, .LayerCount = 6};
    const auto               barrier        = BuildRGStreamingAcquireBarrier(ticket, destination, 5, VK_NULL_HANDLE, range);

    EXPECT_EQ(barrier.sType, VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_NONE);
    EXPECT_EQ(barrier.srcAccessMask, 0u);
    EXPECT_EQ(barrier.dstStageMask, destination.Stage);
    EXPECT_EQ(barrier.dstAccessMask, destination.Access);
    EXPECT_EQ(barrier.oldLayout, ticket.PreReleaseLayout);
    EXPECT_EQ(barrier.newLayout, ticket.PostReleaseLayout);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, 3u);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, 5u);
    EXPECT_EQ(barrier.subresourceRange.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(barrier.subresourceRange.baseMipLevel, 0u);
    EXPECT_EQ(barrier.subresourceRange.levelCount, 4u);
    EXPECT_EQ(barrier.subresourceRange.baseArrayLayer, 0u);
    EXPECT_EQ(barrier.subresourceRange.layerCount, 6u);

    Rendering::Specifications::ImageMemoryBarrierSpecification release = {};
    release.OldLayout                                                  = ImageLayout::TRANSFER_DST_OPTIMAL;
    release.NewLayout                                                  = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
    release.ImageAspectMask                                            = VK_IMAGE_ASPECT_COLOR_BIT;
    release.LayerCount                                                 = 6;
    release.MipLevelCount                                              = 4;
    release.SourceQueueFamily                                          = 3;
    release.DestinationQueueFamily                                     = 5;
    const Rendering::Primitives::ImageMemoryBarrier producer(release);
    const auto&                                     producer_barrier = producer.GetHandle();
    EXPECT_EQ(barrier.oldLayout, producer_barrier.oldLayout);
    EXPECT_EQ(barrier.newLayout, producer_barrier.newLayout);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, producer_barrier.srcQueueFamilyIndex);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, producer_barrier.dstQueueFamilyIndex);
    EXPECT_EQ(barrier.subresourceRange.aspectMask, producer_barrier.subresourceRange.aspectMask);
    EXPECT_EQ(barrier.subresourceRange.baseMipLevel, producer_barrier.subresourceRange.baseMipLevel);
    EXPECT_EQ(barrier.subresourceRange.levelCount, producer_barrier.subresourceRange.levelCount);
    EXPECT_EQ(barrier.subresourceRange.baseArrayLayer, producer_barrier.subresourceRange.baseArrayLayer);
    EXPECT_EQ(barrier.subresourceRange.layerCount, producer_barrier.subresourceRange.layerCount);
}

TEST(RenderGraphSynchronizationTest, StreamingSameFamilyAcquireDoesNotRepeatLayoutTransition)
{
    Hardwares::StreamingUploadTicket ticket = {};
    ticket.PreReleaseLayout                 = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ticket.PostReleaseLayout                = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const auto               destination    = GetRGAccessState(RGAccess::ShaderRead);
    const RGSubresourceRange range          = {.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .LevelCount = 1, .LayerCount = 1};
    for (const uint32_t producer_family : {2u, uint32_t{VK_QUEUE_FAMILY_IGNORED}})
    {
        ticket.ProducerQueueFamily = producer_family;
        const auto barrier         = BuildRGStreamingAcquireBarrier(ticket, destination, 2, VK_NULL_HANDLE, range);
        EXPECT_EQ(barrier.oldLayout, ticket.PostReleaseLayout);
        EXPECT_EQ(barrier.newLayout, ticket.PostReleaseLayout);
        EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
        EXPECT_EQ(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
        EXPECT_EQ(barrier.dstStageMask, destination.Stage);
        EXPECT_EQ(barrier.dstAccessMask, destination.Access);
    }
}

TEST(RenderGraphDeclarationTest, SkyStorageBindingsSeparateSingleMipViewsFromWholeChainSynchronization)
{
    MemoryManager memory = {};
    memory.Initialize(ZMega(4), {});
    auto&       arena = memory.MainArena;
    RenderGraph graph = {};
    graph.FrameArena.Initialize(ZMega(2), 0);
    graph.Passes.init(&arena, 3);
    graph.Resources.init(&arena, 16);
    graph.ResourceIndex.init(&arena, 16);
    graph.ImportedResources.init(&arena, 16);
    graph.ImportedResourceIndex.init(&arena, 16);
    for (uint32_t index = 0; index < 3; ++index)
    {
        graph.Passes.push(MakePass(&arena, "Sky storage"));
    }
    Rendering::Scenes::SkyEnvironment environment = {};
    environment.Initialize({});
    ASSERT_TRUE(environment.SubmitConfig({}, 1));
    Rendering::Scenes::SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeAtmosphere(
        1,
        {
        .Transmittance = {.Index = 1, .Generation = 1},
          .Multiscattering = {.Index = 2, .Generation = 1}
    }));
    ASSERT_TRUE(environment.AttachBakeResource(1, {.Index = 3, .Generation = 1}));
    ASSERT_TRUE(environment.AttachBakeLighting(
        1,
        {
        .DiffuseIrradiance = {.Index = 4, .Generation = 1},
          .SpecularEnvironment = {.Index = 5, .Generation = 1},
          .BrdfIntegrationLut = {.Index = 6, .Generation = 1},
          .BakeSettings = Rendering::ResolveEnvironmentLightingQuality(Rendering::EnvironmentLightingQualityTier::Standard)
    }));
    ASSERT_TRUE(environment.BeginGpuBake(1));
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass = 0;
    SkyAtmosphereSourceRadiancePass source(&environment);
    source.RegisterCompute(nullptr, {}, &builder);
    ASSERT_EQ(graph.Passes[0].Writes.size(), 1u);
    // The full-image sampled transition after source generation requires all
    // mip layouts initialized, independently of the one-mip storage view.
    EXPECT_EQ(graph.Passes[0].Writes[0].BindingKey, nullptr);
    EXPECT_EQ(graph.Passes[0].Writes[0].Range.LevelCount, VK_REMAINING_MIP_LEVELS);
    EXPECT_EQ(graph.Passes[0].Writes[0].Range.LayerCount, VK_REMAINING_ARRAY_LAYERS);
    builder.CurrentPass = 1;
    SkyEnvironmentMipGenerationPass mips(&environment, "sky_atmosphere_mips", true);
    mips.RegisterCompute(nullptr, {}, &builder);
    ASSERT_EQ(graph.Passes[1].Writes.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Writes[0].BindingKey, nullptr);
    EXPECT_EQ(graph.Passes[1].Writes[0].Range.LevelCount, VK_REMAINING_MIP_LEVELS);
    builder.CurrentPass = 2;
    SkyEnvironmentSpecularPrefilterPass specular(&environment);
    specular.RegisterCompute(nullptr, {}, &builder);
    ASSERT_EQ(graph.Passes[2].Writes.size(), 1u);
    EXPECT_EQ(graph.Passes[2].Writes[0].BindingKey, nullptr);
    EXPECT_EQ(graph.Passes[2].Writes[0].Range.LevelCount, VK_REMAINING_MIP_LEVELS);
}

TEST(RenderGraphDeclarationTest, SkyFaceBatchesPreserveEarlierImageContents)
{
    Rendering::Scenes::SkyEnvironment environment = {};
    environment.Initialize({});
    ASSERT_TRUE(environment.SubmitConfig({}, 1));
    Rendering::Scenes::SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeAtmosphere(
        1,
        {
        .Transmittance = {.Index = 1, .Generation = 1},
          .Multiscattering = {.Index = 2, .Generation = 1}
    }));
    ASSERT_TRUE(environment.AttachBakeResource(1, {.Index = 3, .Generation = 1}));
    ASSERT_TRUE(environment.AttachBakeLighting(
        1,
        {
        .DiffuseIrradiance   = {.Index = 4, .Generation = 1},
        .SpecularEnvironment = {.Index = 5, .Generation = 1},
        .BrdfIntegrationLut  = {.Index = 6, .Generation = 1},
        .BakeSettings        = Rendering::ResolveEnvironmentLightingQuality(Rendering::EnvironmentLightingQualityTier::Standard),
    }));
    ASSERT_TRUE(environment.BeginGpuBake(1));
    uint32_t checked_batches = 0;
    for (uint64_t batch = 0; batch < 63; ++batch)
    {
        const auto     stage          = environment.GetActiveBakeStage();
        const uint32_t face           = environment.GetActiveBakeFace();
        const uint32_t slice          = environment.GetActiveBakeSlice();
        const bool     source_stage   = stage == Rendering::Scenes::SkyEnvironmentBakeStage::AtmosphereSourceRadiance;
        const bool     diffuse_stage  = stage == Rendering::Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance;
        const bool     specular_stage = stage == Rendering::Scenes::SkyEnvironmentBakeStage::SpecularEnvironment;
        if (source_stage || diffuse_stage || specular_stage)
        {
            SCOPED_TRACE(batch);
            MemoryManager memory = {};
            memory.Initialize(ZMega(4), {});
            RenderGraph graph = {};
            graph.FrameArena.Initialize(ZMega(2), 0);
            graph.Passes.init(&memory.MainArena, 1);
            graph.Resources.init(&memory.MainArena, 8);
            graph.ResourceIndex.init(&memory.MainArena, 8);
            graph.ImportedResources.init(&memory.MainArena, 8);
            graph.ImportedResourceIndex.init(&memory.MainArena, 8);
            graph.Passes.push(MakePass(&memory.MainArena, "Sky face"));
            RenderGraphResourceBuilder builder = {};
            builder.Initialize(&graph);
            builder.CurrentPass = 0;
            SkyAtmosphereSourceRadiancePass     source(&environment);
            SkyEnvironmentDiffuseIrradiancePass diffuse(&environment);
            SkyEnvironmentSpecularPrefilterPass specular(&environment);
            SkyEnvironmentBakePass*             pass = source_stage ? static_cast<SkyEnvironmentBakePass*>(&source) : diffuse_stage ? static_cast<SkyEnvironmentBakePass*>(&diffuse) : static_cast<SkyEnvironmentBakePass*>(&specular);
            pass->RegisterCompute(nullptr, {}, &builder);
            const uint64_t output_index = source_stage ? 3 : diffuse_stage ? 4 : 5;
            uint32_t       outputs      = 0;
            for (const auto& imported : graph.ImportedResources)
            {
                if (imported.TextureHandle.Index == output_index)
                {
                    ++outputs;
                    EXPECT_EQ(imported.InitialState.Layout, face == 0 && slice == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL);
                }
            }
            EXPECT_EQ(outputs, 1u);
            ++checked_batches;
        }
        ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, stage, face, slice));
        ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, batch + 1));
        ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(batch + 1));
    }
    EXPECT_EQ(checked_batches, 60u);
    EXPECT_TRUE(environment.IsGpuBakeReadyToPublish());
}

TEST(RenderGraphDeclarationTest, ZuiDeclaresBindlessReadsWithoutASceneOrPayload)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.FrameArena.Initialize(ZMega(1), arena.m_mem_page_size);
    graph.Passes.init(&arena, 1);
    graph.Resources.init(&arena, 2);
    graph.ResourceIndex.init(&arena, 2);
    graph.Passes.push(MakePass(&arena, "ZUI"));
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass = 0;

    ZUIPass pass        = {};
    ASSERT_TRUE(pass.Register(nullptr, "ZUI", {}, &builder, nullptr));
    EXPECT_TRUE(graph.Passes[0].ReadsBindless);
    EXPECT_EQ(graph.Passes[0].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[0].Writes.size(), 1u);

    graph.Passes[0].StreamingAcquirePlans.init(&arena, 1);
    graph.SortedPassIndices.init(&arena, 1);
    graph.SortedPassIndices.push(0);
    graph.StreamingUploadTickets.init(&arena, 1);
    // The planner compares timeline identity without issuing any Vulkan calls.
    const ZEngine::Hardwares::StreamingUploadTicket ticket = {
        .Texture             = {.Index = 7, .Generation = 2},
        .CompletionTimeline  = reinterpret_cast<ZEngine::Rendering::Primitives::Semaphore*>(uintptr_t{1}
                      ),
        .CompletionValue     = 42,
        .PreReleaseLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .PostReleaseLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .ProducerQueueFamily = 1,
    };
    graph.StreamingUploadTickets.push(ticket);
    RenderGraphTestAccess::BuildStreamingAcquirePlans(graph);
    ASSERT_EQ(graph.Passes[0].StreamingAcquirePlans.size(), 1u);
    const auto& acquire = graph.Passes[0].StreamingAcquirePlans[0];
    EXPECT_EQ(acquire.Ticket.Texture.Index, 7u);
    EXPECT_EQ(acquire.Ticket.CompletionValue, 42u);
    EXPECT_EQ(acquire.DestinationState.Access, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(acquire.DestinationState.Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ExactBindlessReadKeepsVersionAndShaderStages)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ResourceIndex.init(&arena, 1);

    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    graph.Passes.push(std::move(producer));
    graph.Passes.push(MakePass(&arena, "BindlessConsumer"));

    auto& resource                     = graph.Resources.push_use({});
    resource.Name                      = "LookupTable";
    resource.Kind                      = RGResourceKind::Texture;
    resource.LatestVersion             = 1;
    graph.ResourceIndex[resource.Name] = 0;

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass = 1;
    builder.ReadBindless({0, 1}, 17, RGShaderStages::Vertex | RGShaderStages::Fragment);

    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    const RGPassResource& read = graph.Passes[1].Reads[0];
    EXPECT_EQ(read.Handle.Index, 0u);
    EXPECT_EQ(read.Handle.Version, 1u);
    EXPECT_EQ(read.Access, RGAccess::ShaderRead);
    EXPECT_TRUE(read.HasStateOverride);
    EXPECT_EQ(read.BindlessSlot, 17u);
    EXPECT_EQ(read.StateOverride.Stage, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(read.StateOverride.Access, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(read.StateOverride.Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    Array<uint32_t> order;
    order.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ConditionalReadUsesExactVersionAndExplicitFallback)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 3);
    graph.Resources.init(&arena, 1);
    auto& resource         = graph.Resources.push_use({});
    resource.Kind          = RGResourceKind::Buffer;
    resource.LatestVersion = 1;
    graph.Passes.push(MakePass(&arena, "PredicateProducer"));
    graph.Passes.push(MakePass(&arena, "ConditionalConsumer"));
    graph.Passes.push(MakePass(&arena, "FallbackConsumer"));

    auto device                        = std::make_unique<Hardwares::VulkanDevice>();
    graph.Device                       = device.get();
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);

    device->PhysicalDeviceSupportConditionalRendering = true;
    builder.CurrentPass                               = 1;
    builder.UseConditional({0, 1});

    ASSERT_TRUE(graph.Passes[1].Conditional.Enabled);
    EXPECT_FALSE(graph.Passes[1].Conditional.UsesFallback);
    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Version, 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, RGAccess::ConditionalRead);

    const RGResourceState state = GetRGAccessState(RGAccess::ConditionalRead);
    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT);

    device->PhysicalDeviceSupportConditionalRendering = false;
    builder.CurrentPass                               = 2;
    builder.UseConditional({0, 1}, {.Fallback = RGConditionalFallback::Unconditional});
    EXPECT_TRUE(graph.Passes[2].Conditional.Enabled);
    EXPECT_TRUE(graph.Passes[2].Conditional.UsesFallback);
    EXPECT_TRUE(graph.Passes[2].Reads.empty());
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ImageReadbackKeepsExactImageVersionAndRegion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "Producer"));
    auto& resource         = graph.Resources.push_use({});
    resource.Name          = "ObjectId";
    resource.Kind          = RGResourceKind::Attachment;
    resource.LatestVersion = 3;
    AddWrite(graph.Passes[0], 0, 3);

    const RGImageReadbackRegion region = {
        .Subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .Offset      = {24, 48, 0},
        .Extent      = {1, 1, 1},
    };
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass           = 0;
    const RGReadbackHandle handle = builder.DeclareImageReadback("ObjectIdPickReadback", {0, 3}, region, &TestReadbackCallback, nullptr, sizeof(uint32_t), &TestReadbackCancellation);

    ASSERT_TRUE(handle.Valid());
    ASSERT_EQ(graph.ReadbackRequests.size(), 1u);
    const RGReadbackRequest& request = graph.ReadbackRequests[handle.Index];
    EXPECT_EQ(request.Source.Index, 0u);
    EXPECT_EQ(request.Source.Version, 3u);
    EXPECT_EQ(request.SourceKind, RGReadbackSourceKind::Image);
    EXPECT_EQ(request.ImageRegion.Subresource.aspectMask, VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_EQ(request.ImageRegion.Offset.x, 24);
    EXPECT_EQ(request.ImageRegion.Offset.y, 48);
    EXPECT_EQ(request.ImageRegion.Extent.width, 1u);
    EXPECT_EQ(request.ImageRegion.Extent.height, 1u);
    EXPECT_EQ(request.Size, sizeof(uint32_t));
    EXPECT_EQ(request.Cancellation, &TestReadbackCancellation);

    RGPass sink             = MakePass(&arena, "ObjectIdPickReadback");
    sink.Flags              = RGPassFlags::NeverCull;
    sink.RequiresRenderPass = false;
    sink.InternalOperation  = RGInternalPassOperation::Readback;
    sink.RequestedQueue     = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    AddRead(sink, 0, 3);
    graph.Passes.push(std::move(sink));

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    Array<RGExportedResource> exports;
    exports.init(&arena, 0);
    CullPasses(&arena, graph.Passes, graph.Resources, dependencies, exports);
    EXPECT_TRUE(graph.Passes[1].IsActive());

    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ImageReadbackUsesTheGraphicsQueueForProducerOrdering)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.FrameArena.Initialize(ZMega(1), arena.m_mem_page_size);
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "Producer"));
    auto& resource         = graph.Resources.push_use({});
    resource.Name          = "ObjectId";
    resource.Kind          = RGResourceKind::Attachment;
    resource.LatestVersion = 1;
    AddWrite(graph.Passes[0], 0, 1);

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass                = 0;
    const RGResourceHandle      source = {0, 1};
    const RGImageReadbackRegion region = {
        .Subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .Extent      = {1, 1, 1},
    };
    const RGReadbackHandle readback = builder.DeclareImageReadback("ObjectIdPickReadback", source, region, &TestReadbackCallback, nullptr, sizeof(uint32_t), &TestReadbackCancellation);
    ASSERT_TRUE(readback.Valid());

    RenderGraphTestAccess::AddReadbackPasses(graph);
    ASSERT_EQ(graph.Passes.size(), 2u);
    EXPECT_EQ(graph.Passes[1].RequestedQueue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, RGAccess::TransferRead);

    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, RenderedPreviewReadbackRestoresSampledAfterTheExactCopy)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(2), {});
    auto&       arena = manager.MainArena;
    RenderGraph graph = {};
    graph.FrameArena.Initialize(ZMega(1), arena.m_mem_page_size);
    graph.Passes.init(&arena, 4);
    graph.Resources.init(&arena, 1);
    graph.ReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "Producer"));
    auto& resource         = graph.Resources.push_use({});
    resource.Name          = "Preview";
    resource.Kind          = RGResourceKind::Attachment;
    resource.LatestVersion = 1;
    AddWrite(graph.Passes[0], 0, 1);
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass                = 0;
    const RGImageReadbackRegion region = {
        .Subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
          .Extent = {128, 128, 1}
    };
    ASSERT_TRUE(builder.DeclareImageReadback("PreviewReadback", {0, 1}, region, &TestReadbackCallback, nullptr, 128 * 128 * 4, nullptr, true).Valid());
    RenderGraphTestAccess::AddReadbackPasses(graph);
    ASSERT_EQ(graph.Passes.size(), 3u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, RGAccess::TransferRead);
    EXPECT_EQ(graph.Passes[2].Reads[0].Access, RGAccess::ShaderRead);
    EXPECT_EQ(graph.Passes[2].Reads[0].Handle.Version, 1u);
    // Reverse declaration order to prove the copy->restore edge, not the
    // topological sort's stable tie-break, guarantees the final layout.
    std::swap(graph.Passes[1], graph.Passes[2]);
    Array<uint32_t> order{};
    order.init(&arena, 3);
    Array<RGPassDependency> dependencies{};
    dependencies.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 2u);
    EXPECT_EQ(order[2], 1u);
    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, DisposingSubmittedReadbacksCancelsOutstandingCpuOwnershipOnce)
{
    MemoryManager memory = {};
    memory.Initialize(ZMega(2), {});
    auto device         = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena       = &memory.MainArena;
    RGReadbackRing ring = {};
    ring.Initialize(device.get());
    uint32_t cancellations = 0;
    ring.Allocations.push({.Cancellation = &CountReadbackCancellation, .Context = &cancellations, .Reserved = true, .Submitted = true});
    ring.Dispose();
    EXPECT_EQ(cancellations, 1u);
    ring.Dispose();
    EXPECT_EQ(cancellations, 1u);
    memory.Shutdown();
}

TEST(RenderGraphDeclarationTest, RecordedReadbackSurvivesUntilDeferredGraphicsSubmission)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.ReadbackRequests.init(&arena, 1);
    uint32_t cancellations = 0;
    graph.ReadbackRequests.push({.Name = "DeferredReadback", .Cancellation = &CountReadbackCancellation, .Context = &cancellations, .Recorded = true});

    RenderGraphTestAccess::CancelUnsubmittedReadbacks(graph, true);
    EXPECT_EQ(cancellations, 0u);
    EXPECT_FALSE(graph.ReadbackRequests[0].CancellationDelivered);

    RenderGraphTestAccess::CancelUnsubmittedReadbacks(graph, false);
    EXPECT_EQ(cancellations, 1u);
    EXPECT_TRUE(graph.ReadbackRequests[0].CancellationDelivered);

    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ReadbackKeepsExactBufferVersionAndRequiresTransferSink)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "Producer"));
    auto& resource         = graph.Resources.push_use({});
    resource.Name          = "ExposureHistogram";
    resource.Kind          = RGResourceKind::Buffer;
    resource.LatestVersion = 2;
    AddWrite(graph.Passes[0], 0, 2);

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass           = 0;
    const RGReadbackHandle handle = builder.DeclareReadback("ExposureReadback", {0, 2}, &TestReadbackCallback, nullptr, 64, 16);

    ASSERT_TRUE(handle.Valid());
    ASSERT_EQ(graph.ReadbackRequests.size(), 1u);
    const RGReadbackRequest& request = graph.ReadbackRequests[handle.Index];
    EXPECT_EQ(request.Source.Index, 0u);
    EXPECT_EQ(request.Source.Version, 2u);
    EXPECT_EQ(request.Offset, 16u);
    EXPECT_EQ(request.Size, 64u);
    EXPECT_EQ(resource.BufferUsage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    RGPass sink             = MakePass(&arena, "ExposureReadback");
    sink.Flags              = RGPassFlags::NeverCull;
    sink.RequiresRenderPass = false;
    sink.InternalOperation  = RGInternalPassOperation::Readback;
    sink.RequestedQueue     = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    AddRead(sink, 0, 2);
    graph.Passes.push(std::move(sink));

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    Array<RGExportedResource> exports;
    exports.init(&arena, 0);
    CullPasses(&arena, graph.Passes, graph.Resources, dependencies, exports);
    EXPECT_TRUE(graph.Passes[1].IsActive());

    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);
    BuildQueueBatches(graph.Passes, order, true, false, batches);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, QueryReadbackDependsOnEveryDeclaredQueryWriter)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 3);
    graph.Resources.init(&arena, 0);
    graph.QueryPools.init(&arena, 1);
    graph.QueryPoolIndex.init(&arena, 2);
    graph.QueryReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "OcclusionWriter"));

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass           = 0;
    const RGQueryHandle    pool   = builder.DeclareOcclusionQueryPool("SceneOcclusion", 8);
    const RGReadbackHandle result = builder.DeclareQueryReadback(pool, &TestReadbackCallback, nullptr);
    builder.WriteQueryPool(pool, 2, 3);

    ASSERT_TRUE(pool.Valid());
    ASSERT_TRUE(result.Valid());
    ASSERT_EQ(graph.QueryPools.size(), 1u);
    EXPECT_EQ(graph.QueryPools[pool.Index].QueryCount, 8u);
    ASSERT_EQ(graph.Passes[0].QueryWrites.size(), 1u);
    EXPECT_EQ(graph.Passes[0].QueryWrites[0].FirstQuery, 2u);
    EXPECT_EQ(graph.Passes[0].QueryWrites[0].Count, 3u);

    RGPass resolve               = MakePass(&arena, "OcclusionQueryReadback");
    resolve.Flags                = RGPassFlags::NeverCull;
    resolve.RequiresRenderPass   = false;
    resolve.InternalOperation    = RGInternalPassOperation::QueryReadback;
    resolve.InternalRequestIndex = result.Index;
    resolve.InternalQueryPool    = pool;
    resolve.RequestedQueue       = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    graph.Passes.push(std::move(resolve));

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    ASSERT_EQ(dependencies.size(), 1u);
    EXPECT_EQ(dependencies[0].From, 0u);
    EXPECT_EQ(dependencies[0].To, 1u);

    Array<RGExportedResource> exports;
    exports.init(&arena, 0);
    CullPasses(&arena, graph.Passes, graph.Resources, dependencies, exports);
    EXPECT_TRUE(graph.Passes[0].IsActive());

    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);
    BuildQueueBatches(graph.Passes, order, true, false, batches);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphTransferPassTest, RequestsTransferQueueWithoutRenderPass)
{
    TestTransferPass pass = {};
    EXPECT_EQ(pass.GetRequestedQueue(), ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    EXPECT_FALSE(pass.RequiresRenderPass());
}

TEST(RenderGraphDeclarationValidationTest, RejectsDuplicateProducers)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass first = MakePass(&arena, "First");
    AddWrite(first, 0, 1);
    passes.push(std::move(first));
    RGPass second = MakePass(&arena, "Second");
    AddWrite(second, 0, 1);
    passes.push(std::move(second));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::DuplicateProducer);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, RejectsEnabledConsumerOfDisabledProducer)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass producer  = MakePass(&arena, "DisabledProducer");
    producer.Enabled = false;
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));
    RGPass consumer = MakePass(&arena, "Consumer");
    AddRead(consumer, 0, 1);
    passes.push(std::move(consumer));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::MissingProducer);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, FailedRegistrationRollsBackResourceVersions)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto& arena       = manager.MainArena;

    auto  device      = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena     = &arena;
    RenderGraph graph = {};
    graph.Device      = device.get();
    graph.FrameArena.Initialize(ZMega(1), arena.m_mem_page_size);
    graph.PersistentPasses.init(&arena, 2);
    graph.ImportedResources.init(&arena, 1);
    graph.ImportedResourceIndex.init(&arena, 2);
    graph.QueryPools.init(&arena, 1);
    graph.QueryPoolIndex.init(&arena, 2);

    RenderGraphResourceBuilder   builder   = {};
    RenderGraphResourceInspector inspector = {};
    builder.Initialize(&graph);
    inspector.Initialize(&graph);
    graph.ResourceBuilder         = &builder;
    graph.ResourceInspector       = &inspector;

    FailedRegistrationPass failed = {};
    ValidRegistrationPass  valid  = {};
    graph.AddCallbackPass("Failed", &failed);
    graph.AddCallbackPass("Valid", &valid);
    graph.Register();

    ASSERT_EQ(graph.Resources.size(), 1u);
    EXPECT_EQ(graph.Resources[0].LatestVersion, 1u);
    ASSERT_EQ(graph.Resources[0].Versions.size(), 2u);
    EXPECT_FALSE(graph.Passes[0].Enabled);
    EXPECT_TRUE(graph.Passes[0].Reads.empty());
    EXPECT_TRUE(graph.Passes[0].Writes.empty());
    EXPECT_TRUE(graph.Passes[1].Enabled);
    EXPECT_EQ(valid.Produced.Version, 1u);
    EXPECT_EQ(valid.Consumed.Version, 1u);
    EXPECT_TRUE(ValidatePassDeclarations(&arena, graph.Passes, graph.Resources));

    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, PassLookupCapacityTracksTheRegisteredPipeline)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto device   = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena = &manager.MainArena;
    RenderGraph graph{};
    graph.Device = device.get();
    graph.FrameArena.Initialize(ZMega(1), manager.MainArena.m_mem_page_size);
    graph.PersistentPasses.init(&manager.MainArena, 40);
    graph.ImportedResources.init(&manager.MainArena, 1);
    graph.ImportedResourceIndex.init(&manager.MainArena, 2);
    graph.QueryPools.init(&manager.MainArena, 1);
    graph.QueryPoolIndex.init(&manager.MainArena, 2);
    RenderGraphResourceBuilder   builder{};
    RenderGraphResourceInspector inspector{};
    builder.Initialize(&graph);
    inspector.Initialize(&graph);
    graph.ResourceBuilder   = &builder;
    graph.ResourceInspector = &inspector;
    ValidRegistrationPass callback{};
    char                  names[40][16] = {};
    for (uint32_t i = 0; i < 40; ++i)
    {
        snprintf(names[i], sizeof(names[i]), "Pass-%u", i);
        graph.AddCallbackPass(names[i], &callback);
    }
    graph.Register();
    ASSERT_EQ(graph.Passes.size(), 40u);
    for (uint32_t i = 0; i < 40; ++i)
    {
        const auto* index = graph.PassIndex.find(names[i]);
        ASSERT_NE(index, nullptr);
        EXPECT_EQ(*index, i);
    }
    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, SortsReadAfterWrite)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // Declared as [B reads X, A writes X] — B must still end up after A.
    RGPass b = MakePass(&arena, "B");
    AddRead(b, /*resource=*/0);
    passes.push(std::move(b));

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, /*resource=*/0);
    passes.push(std::move(a));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u); // A (declared second, index 1) must come first
    EXPECT_EQ(order[1], 0u); // B (declared first, index 0) must come second

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, BindsReadToItsDeclaredVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // Declared out of order. B consumes X@1; C produces X@2. Versioning
    // must bind B to A rather than the later producer C.
    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 0, 2);
    passes.push(std::move(c));

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0, 1);
    passes.push(std::move(a));

    Array<uint32_t> order;
    order.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 2u); // A
    EXPECT_EQ(order[1], 0u); // B
    EXPECT_EQ(order[2], 1u); // C

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, CullsPassesUnreachableFromSwapchain)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    RGPass source = MakePass(&arena, "Source");
    AddWrite(source, 0, 1);
    passes.push(std::move(source));
    RGPass composite = MakePass(&arena, "Composite");
    AddRead(composite, 0, 1);
    AddWrite(composite, 1, 1);
    passes.push(std::move(composite));
    RGPass present = MakePass(&arena, "Present");
    AddRead(present, 1, 1);
    AddWrite(present, 2, 1);
    passes.push(std::move(present));
    RGPass dead = MakePass(&arena, "Dead");
    AddWrite(dead, 3, 1);
    passes.push(std::move(dead));

    auto resources        = MakeResources(&arena, 4);
    resources[2].Kind     = RGResourceKind::Swapchain;
    resources[2].External = true;

    Array<uint32_t> order;
    order.init(&arena, 4);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));

    Array<RGExportedResource> exports;
    exports.init(&arena, 1);
    CullPasses(&arena, passes, resources, dependencies, exports);

    EXPECT_TRUE(passes[0].IsActive());
    EXPECT_TRUE(passes[1].IsActive());
    EXPECT_TRUE(passes[2].IsActive());
    EXPECT_TRUE(passes[3].Culled);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, RetainsExplicitExportsAndNeverCullPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));
    RGPass exporter = MakePass(&arena, "Exporter");
    AddRead(exporter, 0, 1);
    AddWrite(exporter, 1, 1);
    passes.push(std::move(exporter));
    RGPass side_effect = MakePass(&arena, "SideEffect");
    side_effect.Flags  = RGPassFlags::NeverCull;
    passes.push(std::move(side_effect));
    RGPass dead = MakePass(&arena, "Dead");
    AddWrite(dead, 2, 1);
    passes.push(std::move(dead));

    auto            resources = MakeResources(&arena, 3);
    Array<uint32_t> order;
    order.init(&arena, 4);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));

    Array<RGExportedResource> exports;
    exports.init(&arena, 1);
    exports.push({
        {1, 1},
        1
    });
    CullPasses(&arena, passes, resources, dependencies, exports);

    EXPECT_TRUE(passes[0].IsActive());
    EXPECT_TRUE(passes[1].IsActive());
    EXPECT_TRUE(passes[2].IsActive());
    EXPECT_TRUE(passes[3].Culled);
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, PreservesDeclarationOrderForIndependentPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddWrite(b, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 2);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, GroupsIndependentPassesIntoOneRecordingLevel)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));

    RGPass left = MakePass(&arena, "Left");
    AddRead(left, 0, 1);
    AddWrite(left, 1, 1);
    passes.push(std::move(left));

    RGPass right = MakePass(&arena, "Right");
    AddRead(right, 0, 1);
    AddWrite(right, 2, 1);
    passes.push(std::move(right));

    RGPass consumer = MakePass(&arena, "Consumer");
    AddRead(consumer, 1, 1);
    AddRead(consumer, 2, 1);
    passes.push(std::move(consumer));

    Array<uint32_t>         order;
    Array<RGPassDependency> dependencies;
    Array<RGTopologyLevel>  levels;
    order.init(&arena, 4);
    dependencies.init(&arena, 8);
    levels.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));
    BuildTopologyLevels(&arena, &arena, order, dependencies, static_cast<uint32_t>(passes.size()), levels);

    ASSERT_EQ(levels.size(), 3u);
    ASSERT_EQ(levels[0].PassIndices.size(), 1u);
    ASSERT_EQ(levels[1].PassIndices.size(), 2u);
    ASSERT_EQ(levels[2].PassIndices.size(), 1u);
    EXPECT_EQ(levels[0].PassIndices[0], 0u);
    EXPECT_EQ(levels[1].PassIndices[0], 1u);
    EXPECT_EQ(levels[1].PassIndices[1], 2u);
    EXPECT_EQ(levels[2].PassIndices[0], 3u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, ExcludesDisabledPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass disabled  = MakePass(&arena, "Disabled");
    disabled.Enabled = false;
    AddWrite(disabled, 0);
    passes.push(std::move(disabled));

    RGPass enabled = MakePass(&arena, "Enabled");
    AddWrite(enabled, 1);
    passes.push(std::move(enabled));

    Array<uint32_t> order;
    order.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, HandlesWriteAfterWrite)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // [A writes X, B writes X], no intervening read — A must stay before B.
    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddWrite(b, 0);
    passes.push(std::move(b));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, KeepsDisplayOverlayAfterToneMapping)
{
    MemoryManager manager{};
    manager.Initialize(ZMega(4), {});
    auto& arena       = manager.MainArena;

    auto  device      = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena     = &arena;
    RenderGraph graph = {};
    graph.Device      = device.get();
    graph.FrameArena.Initialize(ZMega(1), arena.m_mem_page_size);
    graph.PersistentPasses.init(&arena, 2);
    graph.ImportedResources.init(&arena, 1);
    graph.ImportedResourceIndex.init(&arena, 2);
    graph.QueryPools.init(&arena, 1);
    graph.QueryPoolIndex.init(&arena, 2);

    RenderGraphResourceBuilder   builder   = {};
    RenderGraphResourceInspector inspector = {};
    builder.Initialize(&graph);
    inspector.Initialize(&graph);
    graph.ResourceBuilder              = &builder;
    graph.ResourceInspector            = &inspector;

    ToneMappingOutputPass tone_mapping = {};
    DisplayOverlayPass    grid_overlay = {};
    graph.AddCallbackPass("Tone Mapping", &tone_mapping);
    graph.AddCallbackPass("Grid Overlay", &grid_overlay);
    graph.Register();

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);

    ASSERT_EQ(graph.Resources.size(), 1u);
    ASSERT_EQ(graph.Resources[0].LatestVersion, 2u);
    ASSERT_EQ(graph.Passes[1].Writes.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Writes[0].Access, RGAccess::ColorReadWrite);
    EXPECT_EQ(graph.Passes[1].Writes[0].LoadOp, ZEngine::Rendering::Specifications::LoadOperation::LOAD);
    EXPECT_TRUE(ValidatePassDeclarations(&arena, graph.Passes, graph.Resources));

    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    ASSERT_EQ(dependencies.size(), 1u);
    EXPECT_EQ(dependencies[0].From, 0u);
    EXPECT_EQ(dependencies[0].To, 1u);

    graph.FrameArena.Shutdown();
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, HandlesWriteAfterRead)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // [A writes X, B reads X, C writes X] — order must be A, B, C.
    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 0);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, DetectsSimpleCycle)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // A reads Y, writes X. B reads X, writes Y — a 2-node cycle.
    RGPass a = MakePass(&arena, "A");
    AddRead(a, 1);
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    AddWrite(b, 1);
    passes.push(std::move(b));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_FALSE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    EXPECT_EQ(order.size(), 0u);
    EXPECT_TRUE(cycle_idx == 0u || cycle_idx == 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, DetectsLongerCycleWithoutHanging)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // A -> B -> C -> A via read/write chains on resources 0,1,2.
    RGPass a = MakePass(&arena, "A");
    AddRead(a, 2);
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    AddWrite(b, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddRead(c, 1);
    AddWrite(c, 2);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_FALSE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    EXPECT_EQ(order.size(), 0u);
    EXPECT_LT(cycle_idx, 3u);

    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, CompiledComputeBackendPreservesCallbackQueuePolicy)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto& arena                      = manager.MainArena;
    auto  device                     = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena                    = &arena;
    device->HasSeparateComputeQueue  = true;
    device->HasSeparateTransferQueue = true;

    RenderGraph graph                = {};
    graph.Device                     = device.get();
    graph.Passes.init(&arena, 4);
    graph.SortedPassIndices.init(&arena, 4);
    graph.QueueBatches.init(&arena, 4);
    graph.QueueDependencies.init(&arena, 4);
    graph.QueueOwnershipTransfers.init(&arena, 4);
    RenderPasses::ComputePass backend = {};
    backend.Specification.Type        = Rendering::Specifications::RenderPassType::COMPUTE;
    SkyEnvironmentMipGenerationPass sky(nullptr, "sky_hdri_mips", false);
    TestComputePass                 compute = {};

    for (uint32_t index = 0; index < 4; ++index)
    {
        graph.Passes.push(MakePass(&arena, "Queue policy"));
        graph.Passes[index].Handle = &backend;
        graph.SortedPassIndices.push(index);
    }
    graph.Passes[0].Callback = &sky;
    graph.Passes[1].Callback = &compute;
    // A callback-free legacy backend still infers its role from its kind.
    graph.Passes[3].Callback = &compute;
    graph.Passes[3].QueryWrites.push({});

    RenderGraphTestAccess::BuildQueueSchedule(graph);

    EXPECT_EQ(graph.Passes[0].RequestedQueue, Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(graph.Passes[0].Queue, Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(graph.Passes[1].Queue, Rendering::QueueType::COMPUTE_QUEUE);
    EXPECT_EQ(graph.Passes[2].Queue, Rendering::QueueType::COMPUTE_QUEUE);
    EXPECT_EQ(graph.Passes[3].Queue, Rendering::QueueType::GRAPHIC_QUEUE);
    ASSERT_EQ(graph.QueueBatches.size(), 3u);
    EXPECT_EQ(graph.QueueBatches[0].Queue, Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(graph.QueueBatches[1].Queue, Rendering::QueueType::COMPUTE_QUEUE);
    EXPECT_EQ(graph.QueueBatches[1].PassCount, 2u);
    EXPECT_EQ(graph.QueueBatches[2].Queue, Rendering::QueueType::GRAPHIC_QUEUE);

    // Re-scheduling on a single-family device retains the normal fallback.
    device->HasSeparateComputeQueue = false;
    RenderGraphTestAccess::BuildQueueSchedule(graph);
    ASSERT_EQ(graph.QueueBatches.size(), 1u);
    EXPECT_EQ(graph.QueueBatches[0].Queue, Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(graph.QueueBatches[0].PassCount, 4u);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, GroupsContiguousPassesByResolvedQueue)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    passes.push(MakePass(&arena, "Graphics A"));
    passes.push(MakePass(&arena, "Compute A"));
    passes.push(MakePass(&arena, "Compute B"));
    passes.push(MakePass(&arena, "Graphics B"));
    passes[1].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes[2].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;

    Array<uint32_t> order;
    order.init(&arena, 4);
    order.push(0);
    order.push(1);
    order.push(2);
    order.push(3);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 4);

    BuildQueueBatches(passes, order, false, true, batches);

    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[0].FirstPassOrder, 0u);
    EXPECT_EQ(batches[0].PassCount, 1u);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::COMPUTE_QUEUE);
    EXPECT_EQ(batches[1].FirstPassOrder, 1u);
    EXPECT_EQ(batches[1].PassCount, 2u);
    EXPECT_EQ(batches[2].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[2].FirstPassOrder, 3u);
    EXPECT_EQ(batches[2].PassCount, 1u);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, FallsBackToGraphicsWithoutDedicatedFamily)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    passes.push(MakePass(&arena, "Compute"));
    passes.push(MakePass(&arena, "Transfer"));
    passes[0].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes[1].RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;

    Array<uint32_t> order;
    order.init(&arena, 2);
    order.push(0);
    order.push(1);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);

    BuildQueueBatches(passes, order, false, false, batches);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[0].PassCount, 2u);
    EXPECT_EQ(passes[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(passes[1].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, RecordsOneDependencyPerCrossQueueResourceHazard)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 3);
    RGPass compute = MakePass(&arena, "Compute");
    AddWrite(compute, 0, 1);
    compute.RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes.push(std::move(compute));
    RGPass graphics = MakePass(&arena, "Graphics");
    AddRead(graphics, 0, 1);
    AddWrite(graphics, 0, 2);
    passes.push(std::move(graphics));
    RGPass transfer = MakePass(&arena, "Transfer");
    AddRead(transfer, 0, 2);
    transfer.RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    passes.push(std::move(transfer));

    Array<uint32_t> order;
    order.init(&arena, 3);
    order.push(0);
    order.push(1);
    order.push(2);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 3);
    BuildQueueBatches(passes, order, true, true, batches);
    Array<RGQueueDependency> dependencies;
    dependencies.init(&arena, 3);
    Array<RGPassDependency> pass_dependencies;
    pass_dependencies.init(&arena, 2);
    pass_dependencies.push({0, 1});
    pass_dependencies.push({1, 2});

    BuildQueueDependencies(&arena, passes, order, batches, pass_dependencies, dependencies);

    ASSERT_EQ(batches.size(), 3u);
    ASSERT_EQ(dependencies.size(), 2u);
    EXPECT_EQ(dependencies[0].FromBatch, 0u);
    EXPECT_EQ(dependencies[0].ToBatch, 1u);
    EXPECT_EQ(dependencies[1].FromBatch, 1u);
    EXPECT_EQ(dependencies[1].ToBatch, 2u);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, ProducesOwnershipTransferForEachQueueHandOff)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 3);
    RGPass compute = MakePass(&arena, "Compute");
    AddWrite(compute, 0, 1);
    compute.RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes.push(std::move(compute));
    RGPass graphics = MakePass(&arena, "Graphics");
    AddRead(graphics, 0, 1);
    AddWrite(graphics, 0, 2);
    passes.push(std::move(graphics));
    RGPass transfer = MakePass(&arena, "Transfer");
    AddRead(transfer, 0, 2);
    transfer.RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    passes.push(std::move(transfer));

    Array<uint32_t> order;
    order.init(&arena, 3);
    order.push(0);
    order.push(1);
    order.push(2);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 3);
    BuildQueueBatches(passes, order, true, true, batches);
    Array<RGQueueOwnershipTransfer> transfers;
    transfers.init(&arena, 3);

    BuildQueueOwnershipTransfers(&arena, passes, order, batches, 1, transfers);

    ASSERT_EQ(transfers.size(), 2u);
    EXPECT_EQ(transfers[0].ResourceIndex, 0u);
    EXPECT_EQ(transfers[0].FromBatch, 0u);
    EXPECT_EQ(transfers[0].ToBatch, 1u);
    EXPECT_EQ(transfers[1].ResourceIndex, 0u);
    EXPECT_EQ(transfers[1].FromBatch, 1u);
    EXPECT_EQ(transfers[1].ToBatch, 2u);
    manager.Shutdown();
}
