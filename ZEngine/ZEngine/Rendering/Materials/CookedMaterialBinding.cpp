#include <ZEngine/Rendering/Materials/CookedMaterialBinding.h>
#include <ZEngine/Rendering/Textures/CookedTextureUpload.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <utility>

namespace ZEngine::Rendering::Materials
{
    CookedMaterialVersion::CookedMaterialVersion(CookedMaterialVersion&& other) noexcept : m_identity(std::exchange(other.m_identity, {})), m_digest(std::exchange(other.m_digest, {})), m_pin(std::move(other.m_pin)) {}

    CookedMaterialVersion& CookedMaterialVersion::operator=(CookedMaterialVersion&& other) noexcept
    {
        if (this != &other)
        {
            m_pin      = std::move(other.m_pin);
            m_identity = std::exchange(other.m_identity, {});
            m_digest   = std::exchange(other.m_digest, {});
        }
        return *this;
    }

    bool BuildCookedMaterial(const Managers::CookedCpuAssetSet& assets, const Cook::CookAssetIdentity& identity, std::span<const CookedTextureBinding> textures, Meshes::MeshMaterial* output)
    {
        const auto* material = assets.FindMaterial(identity);
        if (!output || !material)
        {
            return false;
        }
        for (size_t i = 0; i < textures.size(); ++i)
        {
            if (textures[i].Identity.Kind != Cook::CookAssetKind::Texture || !textures[i].Identity.IsValid() || !textures[i].Handle.Valid() || textures[i].Handle.Index >= INVALID_MAP_HANDLE || (i && !(textures[i - 1].Identity < textures[i].Identity)))
            {
                return false;
            }
        }
        const auto           vec       = [](const Core::Containers::FixedArray<float, 4>& value) { return Core::Maths::Vec4f{value[0], value[1], value[2], value[3]}; };
        Meshes::MeshMaterial candidate = {};
        candidate.AmbientColor         = vec(material->AmbientColor);
        candidate.AlbedoColor          = vec(material->AlbedoColor);
        candidate.EmissiveColor        = vec(material->EmissiveColor);
        candidate.RoughnessColor       = vec(material->RoughnessColor);
        candidate.SpecularColor        = vec(material->SpecularColor);
        candidate.Factors              = vec(material->Factors);
        candidate.AlphaMode            = material->AlphaMode;
        uint32_t* maps[]               = {&candidate.AlbedoMap, &candidate.EmissiveMap, &candidate.NormalMap, &candidate.OpacityMap, &candidate.SpecularMap};
        static_assert(std::size(maps) == static_cast<size_t>(Importers::AssetCodec::MaterialTextureSlot::Count));
        for (size_t i = 0; i < material->Textures.size(); ++i)
        {
            const auto& texture = material->Textures[i];
            if (texture.UUID.is_nil())
            {
                continue;
            }
            const Cook::CookAssetIdentity expected = {Cook::CookAssetKind::Texture, texture.UUID};
            const auto*                   version  = assets.FindVersion(expected);
            if (!version || !assets.FindTexture(expected) || textures.empty())
            {
                return false;
            }
            const auto binding = std::lower_bound(textures.begin(), textures.end(), expected, [](const CookedTextureBinding& value, const Cook::CookAssetIdentity& key) { return value.Identity < key; });
            if (binding == textures.end() || binding->Identity != expected || binding->ObjectDigest != version->ObjectDigest)
            {
                return false;
            }
            *maps[i] = static_cast<uint32_t>(binding->Handle.Index);
        }
        *output = candidate;
        return true;
    }

    SceneTextureDescriptorState PrepareCookedMaterial(const Managers::CookedCpuAssetSet& assets, const Cook::CookAssetIdentity& identity, std::span<Textures::CookedTextureUpload* const> uploads, RenderResourceManager& manager, CookedMaterialVersion* output)
    {
        const auto* version = assets.FindVersion(identity);
        if (!output || *output || !version || identity.Kind != Cook::CookAssetKind::Material)
        {
            return SceneTextureDescriptorState::Failed;
        }
        RenderResourceManager::MaterialVersionPin pin   = {};
        const auto                                state = PrepareCookedMaterial(assets, identity, uploads, manager, &pin);
        if (state == SceneTextureDescriptorState::Ready)
        {
            output->m_identity = identity;
            output->m_digest   = version->ObjectDigest;
            output->m_pin      = std::move(pin);
        }
        return state;
    }

    SceneTextureDescriptorState PrepareCookedMaterial(const Managers::CookedCpuAssetSet& assets, const Cook::CookAssetIdentity& identity, std::span<Textures::CookedTextureUpload* const> uploads, RenderResourceManager& manager, RenderResourceManager::MaterialVersionPin* output)
    {
        const auto* material = assets.FindMaterial(identity);
        if (!output || *output || !material || uploads.size() > 5)
        {
            return SceneTextureDescriptorState::Failed;
        }
        std::array<bool, 5>     used        = {};
        std::array<uint32_t, 5> map_uploads = {};
        map_uploads.fill(UINT32_MAX);
        for (size_t i = 0; i < uploads.size(); ++i)
        {
            if (!uploads[i] || uploads[i]->Identity().Kind != Cook::CookAssetKind::Texture || !uploads[i]->Identity().IsValid() || (i && !(uploads[i - 1]->Identity() < uploads[i]->Identity())))
            {
                return SceneTextureDescriptorState::Failed;
            }
            const auto* version = assets.FindVersion(uploads[i]->Identity());
            if (!version || !assets.FindTexture(uploads[i]->Identity()) || version->ObjectDigest != uploads[i]->Digest())
            {
                return SceneTextureDescriptorState::Failed;
            }
        }
        for (size_t map = 0; map < material->Textures.size(); ++map)
        {
            const auto& uuid = material->Textures[map].UUID;
            if (uuid.is_nil())
            {
                continue;
            }
            const Cook::CookAssetIdentity expected = {Cook::CookAssetKind::Texture, uuid};
            const auto                    it       = std::lower_bound(uploads.begin(), uploads.end(), expected, [](const auto* upload, const auto& key) { return upload->Identity() < key; });
            if (it == uploads.end() || (*it)->Identity() != expected)
            {
                return SceneTextureDescriptorState::Failed;
            }
            const auto index = static_cast<uint32_t>(it - uploads.begin());
            used[index]      = true;
            map_uploads[map] = index;
        }
        std::array<CookedTextureBinding, 5>                     bindings = {};
        std::array<RenderResourceManager::TextureVersionPin, 5> pins     = {};
        for (size_t i = 0; i < uploads.size(); ++i)
        {
            if (!used[i])
            {
                return SceneTextureDescriptorState::Failed;
            }
            const auto state = uploads[i]->Poll();
            if (state == Textures::CookedTextureUpload::Readiness::Failed)
            {
                return SceneTextureDescriptorState::Failed;
            }
            if (state != Textures::CookedTextureUpload::Readiness::Ready)
            {
                return SceneTextureDescriptorState::Pending;
            }
            const auto* binding = uploads[i]->ReadyBinding();
            if (!binding)
            {
                return SceneTextureDescriptorState::Pending;
            }
            bindings[i] = *binding;
            pins[i]     = uploads[i]->RetainPreparedVersion();
            if (!pins[i])
            {
                return SceneTextureDescriptorState::Failed;
            }
        }
        Meshes::MeshMaterial gpu_material = {};
        if (!BuildCookedMaterial(assets, identity, {bindings.data(), uploads.size()}, &gpu_material))
        {
            return SceneTextureDescriptorState::Failed;
        }
        const RenderResourceManager::TextureVersionPin* map_pins[5] = {};
        for (size_t map = 0; map < map_uploads.size(); ++map)
        {
            if (map_uploads[map] != UINT32_MAX)
            {
                map_pins[map] = &pins[map_uploads[map]];
            }
        }
        return manager.PrepareExplicitMaterialVersion(gpu_material, map_pins, output);
    }
} // namespace ZEngine::Rendering::Materials
