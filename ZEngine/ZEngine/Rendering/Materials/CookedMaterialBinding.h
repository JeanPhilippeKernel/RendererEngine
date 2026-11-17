#pragma once
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <span>

namespace ZEngine::Rendering::Textures
{
    class CookedTextureUpload;
}

namespace ZEngine::Rendering::Materials
{
    /// An upload owner associates its exact cooked content with a GPU handle.
    /// This is only a binding declaration: the renderer must separately retain
    /// that handle's generation and prove upload and descriptor readiness.
    struct CookedTextureBinding
    {
        Cook::CookAssetIdentity Identity     = {};
        Cook::Digest256         ObjectDigest = {};
        Textures::TextureHandle Handle       = {};
    };

    /// Exact cooked material identity and the retained shader/texture version.
    /// Render-owner only; its resource manager outlives this object.
    class CookedMaterialVersion final
    {
    public:
        CookedMaterialVersion()                                        = default;
        CookedMaterialVersion(const CookedMaterialVersion&)            = delete;
        CookedMaterialVersion& operator=(const CookedMaterialVersion&) = delete;
        CookedMaterialVersion(CookedMaterialVersion&&) noexcept;
        CookedMaterialVersion& operator=(CookedMaterialVersion&&) noexcept;
        explicit               operator bool() const
        {
            return bool(m_pin);
        }
        const Cook::CookAssetIdentity& Identity() const noexcept
        {
            return m_identity;
        }
        const Cook::Digest256& Digest() const noexcept
        {
            return m_digest;
        }
        const RenderResourceManager::MaterialVersionPin& Pin() const noexcept
        {
            return m_pin;
        }
        void Reset()
        {
            m_pin.Reset();
            m_identity = {};
            m_digest   = {};
        }

    private:
        friend SceneTextureDescriptorState        PrepareCookedMaterial(const Managers::CookedCpuAssetSet&, const Cook::CookAssetIdentity&, std::span<Textures::CookedTextureUpload* const>, RenderResourceManager&, CookedMaterialVersion*);
        Cook::CookAssetIdentity                   m_identity = {};
        Cook::Digest256                           m_digest   = {};
        RenderResourceManager::MaterialVersionPin m_pin      = {};
    };

    /// Build shader values from the selected immutable material and explicitly
    /// supplied texture versions. Bindings are sorted and unique by typed
    /// identity. Required maps never use a fallback; nil maps use the shader's
    /// defined sentinel. Failure leaves output unchanged.
    [[nodiscard]] bool                        BuildCookedMaterial(const Managers::CookedCpuAssetSet&, const Cook::CookAssetIdentity&, std::span<const CookedTextureBinding>, Meshes::MeshMaterial* output);

    /// Prepare a retained material directly from the verified CPU closure and
    /// its exact uploaded texture transactions. Inputs are sorted, unique and
    /// limited to the material's required maps. Pending/Failed preserve output.
    /// Ready retains the exact texture generations and descriptor publications;
    /// the assets and upload transactions may then retire independently. The
    /// render owner calls this after arranging graphics-family acquire work.
    [[nodiscard]] SceneTextureDescriptorState PrepareCookedMaterial(const Managers::CookedCpuAssetSet&, const Cook::CookAssetIdentity&, std::span<Textures::CookedTextureUpload* const>, RenderResourceManager&, RenderResourceManager::MaterialVersionPin* output);
    [[nodiscard]] SceneTextureDescriptorState PrepareCookedMaterial(const Managers::CookedCpuAssetSet&, const Cook::CookAssetIdentity&, std::span<Textures::CookedTextureUpload* const>, RenderResourceManager&, CookedMaterialVersion* output);
} // namespace ZEngine::Rendering::Materials
