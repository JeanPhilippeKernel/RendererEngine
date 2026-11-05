#include <ZEngine/Cook/CookObject.h>
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::Cook
{
    namespace
    {
        bool ValidExtension(Core::Containers::StringView extension)
        {
            if (!extension.data() || extension.empty() || extension.size() > 16)
            {
                return false;
            }
            for (const char value : extension)
            {
                if ((value < 'a' || value > 'z') && (value < '0' || value > '9'))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool IsCookObjectPath(const Core::VFS::VFSRelativePath& path, const Digest256& digest, uint64_t size, Core::Containers::StringView extension) noexcept
    {
        if (!path.IsValid() || !size || size > ZGiga(16) || !ValidExtension(extension))
        {
            return false;
        }
        const auto text = path.View();
        const auto hex  = digest.Hex();
        return text.size() == 73 + extension.size() && Core::Containers::StringView(text.data(), 8) == "objects/" && Core::Containers::StringView(text.data() + 8, 64) == Core::Containers::StringView(hex.data(), 64) && text[72] == '.' && Core::Containers::StringView(text.data() + 73, extension.size()) == extension;
    }

    Core::VFS::VFSResult<void> MakeCookObjectDescriptor(const Digest256& digest, uint64_t size, Core::Containers::StringView extension, CookObjectDescriptor* output)
    {
        if (!output || !ValidExtension(extension))
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        if (!size || size > ZGiga(16))
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        char       text[73 + 16] = {};
        const auto hex           = digest.Hex();
        Helpers::secure_memcpy(text, sizeof(text), "objects/", 8);
        Helpers::secure_memcpy(text + 8, sizeof(text) - 8, hex.data(), 64);
        text[72] = '.';
        Helpers::secure_memcpy(text + 73, sizeof(text) - 73, extension.data(), extension.size());
        const auto path = Core::VFS::VFSRelativePath::Parse({text, 73 + extension.size()});
        if (path.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(path.Error());
        }
        *output = {path.Value(), digest, size};
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Cook
