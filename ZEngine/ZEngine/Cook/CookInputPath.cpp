#include <ZEngine/Cook/CookInputPath.h>

namespace ZEngine::Cook
{
    Core::VFS::VFSResult<CookInputPath> ParseCookInputPath(Core::Containers::StringView text)
    {
        if (text.empty() || !text.data())
        {
            return Core::VFS::VFSResult<CookInputPath>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const bool context = text[0] == '/';
        const auto strict  = Core::VFS::VFSRelativePath::Parse(text.data() + (context ? 1 : 0), text.size() - (context ? 1 : 0));
        if (strict.Failed())
        {
            return Core::VFS::VFSResult<CookInputPath>::Fail(strict.Error());
        }
        CookInputPath result = {};
        if (context)
        {
            const auto parsed = Core::VFS::VFSPath::Parse(text.data(), text.size());
            if (parsed.Failed())
            {
                return Core::VFS::VFSResult<CookInputPath>::Fail(parsed.Error());
            }
            result.Context = parsed.Value();
        }
        else
        {
            if (!IsCookLogicalPath(strict.Value()))
            {
                return Core::VFS::VFSResult<CookInputPath>::Fail(Core::VFS::VFSError::InvalidPath);
            }
            result.Logical = strict.Value();
        }
        return Core::VFS::VFSResult<CookInputPath>::Ok(result);
    }
} // namespace ZEngine::Cook
