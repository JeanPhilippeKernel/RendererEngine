#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/CookedAssetReader.h>
#include <algorithm>
#include <cstdio>
#include <utility>

namespace ZEngine::Managers
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;
    namespace Image = Importers::ImageCodec;

    namespace
    {
        const Cook::CookManifestAsset* Match(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Cook::CookAssetKind kind)
        {
            const auto* asset = package.FindSelectedAsset(identity);
            return asset && identity.Kind == kind && asset->Format && !object.Bytes().empty() && object.Descriptor() == asset->Result.Object ? asset : nullptr;
        }

        bool MeshDependenciesMatch(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const Importers::AssetMesh& mesh)
        {
            const auto dependencies = package.Dependencies(identity);
            size_t     matched      = 0;
            for (size_t index = 0; index < mesh.SubMeshes.size(); ++index)
            {
                const auto& uuid = mesh.SubMeshes[index].MaterialUUID;
                if (uuid.is_nil())
                {
                    continue;
                }
                if (dependencies.empty())
                {
                    return false;
                }
                char      slot[48] = {};
                const int length   = std::snprintf(slot, sizeof(slot), "submeshes/%zu/material", index);
                if (length <= 0 || static_cast<size_t>(length) >= sizeof(slot))
                {
                    return false;
                }
                const Core::Containers::StringView key(slot, static_cast<size_t>(length));
                const auto                         dependency = std::lower_bound(dependencies.begin(), dependencies.end(), key, [](const Cook::CookDependency& left, Core::Containers::StringView right) { return Cook::CookDependencySlotBefore(left.Slot, right); });
                const Cook::CookAssetIdentity      expected   = {Cook::CookAssetKind::Material, uuid};
                if (dependency == dependencies.end() || dependency->Identity != expected || !(dependency->Slot == key) || !package.FindSelectedAsset(expected))
                {
                    return false;
                }
                ++matched;
            }
            return matched == dependencies.size();
        }

        bool MaterialDependenciesMatch(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const Codec::MaterialAssetDocument& material)
        {
            const auto dependencies = package.Dependencies(identity);
            size_t     cursor       = 0;
            for (size_t index = 0; index < material.Textures.size(); ++index)
            {
                const auto& texture = material.Textures[index];
                if (texture.UUID.is_nil())
                {
                    continue;
                }
                if (cursor >= dependencies.size())
                {
                    return false;
                }
                const auto&                   dependency = dependencies[cursor++];
                const Cook::CookAssetIdentity expected   = {Cook::CookAssetKind::Texture, texture.UUID};
                const auto*                   child      = package.FindSelectedAsset(expected);
                if (dependency.Identity != expected || !(dependency.Slot == Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(index))) || !child || !(Core::Containers::StringView(texture.Path) == child->Result.Object.Path.View()))
                {
                    return false;
                }
            }
            return cursor == dependencies.size();
        }

        cstring TextureFormat(Image::EncodedImageKind kind)
        {
            switch (kind)
            {
                case Image::EncodedImageKind::PNG:
                    return "image-png";
                case Image::EncodedImageKind::JPEG:
                    return "image-jpeg";
                case Image::EncodedImageKind::BMP:
                    return "image-bmp";
                case Image::EncodedImageKind::TGA:
                    return "image-tga";
                case Image::EncodedImageKind::GIF:
                    return "image-gif";
                case Image::EncodedImageKind::PSD:
                    return "image-psd";
                case Image::EncodedImageKind::PIC:
                    return "image-pic";
                default:
                    return nullptr;
            }
        }

        const Cook::CookManifestAsset* MatchTexture(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object)
        {
            const auto* asset = Match(package, identity, object, Cook::CookAssetKind::Texture);
            return asset && package.Dependencies(identity).empty() ? asset : nullptr;
        }

        bool MatchesTextureFormat(const Cook::CookManifestAsset& asset, Image::EncodedImageKind kind)
        {
            const auto format = TextureFormat(kind);
            return format && Core::Containers::StringView(asset.Format->Key) == format;
        }
    } // namespace

    bool CookedAssetReader::MatchesMeshDependencies(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const Importers::AssetMesh& mesh)
    {
        const auto* asset = package.FindSelectedAsset(identity);
        return asset && asset->Format && identity.Kind == Cook::CookAssetKind::Mesh && mesh.MeshUUID == identity.UUID && Core::Containers::StringView(asset->Format->Key) == "zemesh-v1-le64" && MeshDependenciesMatch(package, identity, mesh);
    }

    bool CookedAssetReader::MatchesMaterialDependencies(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const Codec::MaterialAssetDocument& material)
    {
        const auto* asset = package.FindSelectedAsset(identity);
        return asset && asset->Format && identity.Kind == Cook::CookAssetKind::Material && material.UUID == identity.UUID && Core::Containers::StringView(asset->Format->Key) == "zematerial-v1-json" && MaterialDependenciesMatch(package, identity, material);
    }

    VFS::VFSResult<void> CookedAssetReader::ReadMesh(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Core::Memory::ArenaAllocator* arena, Core::Memory::IWorkingMemoryBudget& budget, Importers::AssetMesh* output, Importers::AssetNodeHierarchy* hierarchy, const Codec::MeshAssetReadLimits& limits)
    {
        if (!arena || !output || !hierarchy)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto* asset = Match(package, identity, object, Cook::CookAssetKind::Mesh);
        if (!asset || !(Core::Containers::StringView(asset->Format->Key) == "zemesh-v1-le64"))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(&budget, Codec::MeshAssetReadFixedWorkingBytes() + sizeof(Importers::AssetMesh) + sizeof(Importers::AssetNodeHierarchy) + sizeof(fixed)))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        const auto    temporary = Core::Memory::BeginTempArena(arena);
        VFS::VFSError error     = VFS::VFSError::OK;
        {
            Importers::AssetMesh          candidate           = {};
            Importers::AssetNodeHierarchy candidate_hierarchy = {};
            const auto                    decoded             = Codec::ReadMeshAsset(object.Source(), arena, &candidate, &candidate_hierarchy, identity.UUID, limits);
            if (decoded.Failed())
            {
                error = decoded.Error();
            }
            else if (!MatchesMeshDependencies(package, identity, candidate))
            {
                error = VFS::VFSError::Corrupted;
            }
            else
            {
                *output    = std::move(candidate);
                *hierarchy = std::move(candidate_hierarchy);
            }
        }
        if (error != VFS::VFSError::OK)
        {
            Core::Memory::EndTempArena(temporary);
            return VFS::VFSResult<void>::Fail(error);
        }
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedAssetReader::ReadMaterial(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Codec::MaterialAssetReader& reader, const Codec::MaterialAssetDocument** output)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto* asset = Match(package, identity, object, Cook::CookAssetKind::Material);
        if (!asset || !(Core::Containers::StringView(asset->Format->Key) == "zematerial-v1-json"))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        const auto bytes   = object.Bytes();
        const auto decoded = reader.Read({bytes.data(), bytes.size()}, identity.UUID);
        if (decoded.Failed())
        {
            return VFS::VFSResult<void>::Fail(decoded.Error());
        }
        if (!decoded.Value() || !MatchesMaterialDependencies(package, identity, *decoded.Value()))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        *output = decoded.Value();
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedAssetReader::ValidateTexture(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Core::Memory::IAllocationRoute& route, const Image::EncodedImageLimits& limits, Image::EncodedImageDescription* output)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto* asset = MatchTexture(package, identity, object);
        if (!asset)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        Image::EncodedImageDescription description = {};
        const auto                     validated   = Image::ValidateEncodedImage(object.Source(), route, limits, &description);
        if (validated.Failed())
        {
            return validated;
        }
        if (!MatchesTextureFormat(*asset, description.Kind))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        *output = description;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedAssetReader::ReadTexture(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Core::Memory::IAllocationRoute& route, const Image::EncodedImageLimits& limits, Image::DecodedImage* output)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto* asset = MatchTexture(package, identity, object);
        if (!asset)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        Image::DecodedImage image   = {};
        const auto          decoded = Image::DecodeEncodedImage(object.Source(), route, limits, &image);
        if (decoded.Failed())
        {
            return decoded;
        }
        if (!MatchesTextureFormat(*asset, image.Description().Kind))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        *output = std::move(image);
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookedAssetReader::ReadEnvironment(const CookedPackageSnapshot& package, const Cook::CookAssetIdentity& identity, const CookedObjectSnapshot& object, Core::Containers::ArrayView<uint8_t> stream, Codec::EnvironmentMapFileHeader* output, const Codec::EnvironmentMapReadLimits& limits)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto* asset = Match(package, identity, object, Cook::CookAssetKind::EnvironmentMap);
        if (!asset || !(Core::Containers::StringView(asset->Format->Key) == "zenvmap-v2-le64") || !package.Dependencies(identity).empty() || object.Bytes().size() < sizeof(Codec::EnvironmentMapFileHeader))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        Codec::EnvironmentMapFileHeader header = {};
        (void) Helpers::secure_memcpy(&header, sizeof(header), object.Bytes().data(), sizeof(header));
        // The immutable object digest, not the source-file receipt, is the
        // runtime identity. The header's source hash is provenance only.
        return Codec::ReadEnvironmentMapAsset(object.Source(), stream, output, header.SourceHash, limits);
    }
} // namespace ZEngine::Managers
