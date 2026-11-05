#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Image = Importers::ImageCodec;

    namespace
    {
        cstring ImageFormat(Image::EncodedImageKind kind)
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

        VFS::VFSResult<void> ValidateTexture(const CookAssetInput& input, ICookAllocator& scratch, CookStage stage, CookDiagnostics* diagnostics, cstring* format)
        {
            Detail::CapturedByteSource      bridge(*input.Artifact);
            CookCodecAllocationRoute        route(scratch);
            Image::EncodedImageDescription  description = {};
            const Image::EncodedImageLimits limits      = {input.Limits->MaxEncodedAssetBytes, input.Limits->MaxDecodedAssetBytes, input.Limits->MaxImageDimension};
            const auto                      validated   = Image::ValidateEncodedImage(bridge.View(), route, limits, &description);
            const auto                      error       = route.Error() != VFS::VFSError::OK ? route.Error() : validated.Error();
            if (error != VFS::VFSError::OK)
            {
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(error), error, "Texture does not satisfy the bounded supported CPU image contract.");
            }
            *format = ImageFormat(description.Kind);
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> InspectTexture(const void*, const CookAssetInput& input, ICookAllocator& scratch, CookAssetInspection* output, CookDiagnostics* diagnostics)
        {
            if (!output)
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Texture inspection requires result storage.");
            }
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Texture, CookStage::Discover, diagnostics, &metadata_key);
            if (metadata.Failed())
            {
                return metadata;
            }
            cstring    format    = nullptr;
            const auto validated = ValidateTexture(input, scratch, CookStage::Discover, diagnostics, &format);
            return validated.Failed() ? validated : output->Assign(format, input.Artifact->Digest(), metadata_key, {}, scratch);
        }

        VFS::VFSResult<void> EncodeTexture(const void*, const CookAssetInput& input, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, CookTarget target, ICookByteSink& sink, ICookAllocator& scratch, CookDiagnostics* diagnostics)
        {
            if (target != CookTarget::DesktopVulkanLE64)
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Texture target is unsupported.");
            }
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Texture, CookStage::Encode, diagnostics, &metadata_key);
            if (metadata.Failed())
            {
                return metadata;
            }
            if (dependencies.size())
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Texture is a leaf and cannot have resolved dependencies.");
            }
            cstring    format    = nullptr;
            const auto validated = ValidateTexture(input, scratch, CookStage::Encode, diagnostics, &format);
            if (validated.Failed())
            {
                return validated;
            }
            // Decoder storage is already released. Only a bounded stream lives
            // during passthrough; never retain a second encoded image copy.
            CookWorkspace stream    = {};
            const size_t  count     = static_cast<size_t>(std::min<uint64_t>(input.Artifact->Size(), ZKilo(64)));
            const auto    allocated = stream.Initialize(scratch, count, CookMemoryCategory::Codecs);
            if (allocated.Failed())
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(allocated.Error()), allocated.Error(), "Texture stream allocation failed.");
            }
            auto*      bytes  = static_cast<uint8_t*>(stream.Arena()->AllocateNoZero(count, alignof(uint8_t)));
            const auto copied = Detail::CopyCapturedAsset(*input.Artifact, sink, {bytes, count}, input.Limits->MaxEncodedAssetBytes, scratch);
            return copied.Failed() ? Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(copied.Error()), copied.Error(), "Validated texture passthrough could not be completed.") : copied;
        }
    } // namespace

    CookAdapterFns CookTextureAdapter() noexcept
    {
        return {CookAssetKind::Texture, 1, nullptr, InspectTexture, EncodeTexture};
    }
} // namespace ZEngine::Cook
