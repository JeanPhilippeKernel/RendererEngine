#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorSceneIO.h>
#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <filesystem>
#if defined(_WIN32)
#include <Windows.h>
#endif

using namespace ZEngine::Core::VFS;
using namespace ZEngine::ECS;

namespace Tetragrama
{
    namespace
    {
        bool ValidPathUtf8(std::string_view text)
        {
            if (text.size() >= VFS_MAX_PATH)
            {
                return false;
            }
            for (size_t i = 0; i < text.size();)
            {
                const auto lead = static_cast<uint8_t>(text[i++]);
                if (!lead)
                {
                    return false;
                }
                if (lead < 0x80)
                {
                    continue;
                }
                uint32_t value = 0, minimum = 0, continuation = 0;
                if (lead >= 0xc2 && lead <= 0xdf)
                {
                    value        = lead & 0x1f;
                    minimum      = 0x80;
                    continuation = 1;
                }
                else if (lead >= 0xe0 && lead <= 0xef)
                {
                    value        = lead & 0x0f;
                    minimum      = 0x800;
                    continuation = 2;
                }
                else if (lead >= 0xf0 && lead <= 0xf4)
                {
                    value        = lead & 0x07;
                    minimum      = 0x10000;
                    continuation = 3;
                }
                else
                {
                    return false;
                }
                if (continuation > text.size() - i)
                {
                    return false;
                }
                for (uint32_t lane = 0; lane < continuation; ++lane)
                {
                    const auto byte = static_cast<uint8_t>(text[i++]);
                    if ((byte & 0xc0) != 0x80)
                    {
                        return false;
                    }
                    value = (value << 6) | (byte & 0x3f);
                }
                if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                {
                    return false;
                }
            }
            return true;
        }

        bool NativePathFromUtf8(std::string_view text, std::filesystem::path& output)
        {
            if (!ValidPathUtf8(text))
            {
                return false;
            }
            if (text.empty())
            {
                output.clear();
                return true;
            }
#if defined(_WIN32)
            ZEngine::Core::Containers::FixedArray<wchar_t, VFS_MAX_PATH> native = {};
            const int                                                    count  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), native.data(), static_cast<int>(native.size()));
            if (count <= 0)
                return false;
            // Native UTF-16 construction avoids locale-dependent conversion
            // and its encoding exceptions in std::filesystem's narrow overload.
            output.assign(native.data(), native.data() + count);
#else
            output.assign(text.begin(), text.end());
#endif
            return true;
        }

        bool NativePathToUtf8(const std::filesystem::path& path, bool generic, std::string& output)
        {
            const auto& native = path.native();
            if (native.size() >= VFS_MAX_PATH)
            {
                return false;
            }
#if defined(_WIN32)
            if (native.empty())
            {
                output.clear();
                return true;
            }
            ZEngine::Core::Containers::FixedArray<char, VFS_MAX_PATH> utf8  = {};
            const int                                                 count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), static_cast<int>(native.size()), utf8.data(), static_cast<int>(utf8.size() - 1), nullptr, nullptr);
            if (count <= 0)
                return false;
            if (generic)
                for (int i = 0; i < count; ++i)
                    if (utf8[i] == '\\')
                        utf8[i] = '/';
            output.assign(utf8.data(), static_cast<size_t>(count));
#else
            (void) generic;
            if (!ValidPathUtf8(native))
            {
                return false;
            }
            output = native;
#endif
            return true;
        }
    } // namespace

    bool Editor::SceneFileOwner::MatchesWorkspace(cstring current) const
    {
        if (!HasWorkspace)
        {
            return !current;
        }
        if (!current)
        {
            return false;
        }
        // Stop at the first mismatch, including an early NUL in a shorter owner.
        // Never run strlen or compare the unused tail of the retained buffer.
        for (size_t i = 0; i < WorkspaceLength; ++i)
        {
            if (!current[i] || current[i] != Workspace[i])
            {
                return false;
            }
        }
        return current[WorkspaceLength] == '\0';
    }

    bool Editor::SceneFileOwner::Capture(const Editor& editor)
    {
        Reset();
        if (!editor.Session.OnOwnerThread())
        {
            return false;
        }
        auto* owner = static_cast<EditorScene*>(editor.CurrentScene);
        if (!owner || owner->Session != &editor.Session || !editor.Session.Scene())
        {
            return false;
        }
        HasWorkspace = editor.WorkingSpacePath != nullptr;
        if (HasWorkspace)
        {
            while (WorkspaceLength < Workspace.size() && editor.WorkingSpacePath[WorkspaceLength])
            {
                ++WorkspaceLength;
            }
            if (WorkspaceLength == Workspace.size() || ZEngine::Helpers::secure_memcpy(Workspace.data(), Workspace.size(), editor.WorkingSpacePath, WorkspaceLength + 1) != 0)
            {
                return false;
            }
        }
        Config = editor.Configuration;
        if (Config && (Config->WorkingSpacePath.size() != WorkspaceLength || !MatchesWorkspace(Config->WorkingSpacePath.c_str())))
        {
            return false;
        }
        SceneOwner = owner;
        SceneUUID  = owner->UUID;
        World      = editor.Session.Scene();
        Window     = editor.CurrentWindow;
        Epoch      = editor.Session.SceneEpoch();
        return true;
    }

    bool Editor::SceneFileOwner::Matches(const Editor& editor) const
    {
        if (!Epoch || !editor.Session.OnOwnerThread() || Epoch != editor.Session.SceneEpoch() || SceneOwner != editor.CurrentScene || World != editor.Session.Scene() || Window != editor.CurrentWindow || Config != editor.Configuration)
        {
            return false;
        }
        const auto* owner = static_cast<const EditorScene*>(editor.CurrentScene);
        return owner && owner->Session == &editor.Session && owner->UUID == SceneUUID && MatchesWorkspace(editor.WorkingSpacePath) && (!Config || (Config->WorkingSpacePath.size() == WorkspaceLength && MatchesWorkspace(Config->WorkingSpacePath.c_str())));
    }

    bool TrySceneSourceName(cstring name, std::string_view* out_name)
    {
        if (!out_name)
        {
            return false;
        }
        size_t length = 0;
        if (name)
        {
            while (length <= SceneDocument::MaxNameBytes && name[length])
            {
                ++length;
            }
        }
        if (length > SceneDocument::MaxNameBytes)
        {
            return false;
        }
        *out_name = name ? std::string_view(name, length) : std::string_view{};
        return true;
    }

    bool IsSceneSourcePath(const VFSPath& path)
    {
        // These namespaces belong to other mounted owners, not the project root.
        return path.IsValid() && !path.IsRoot() && path.Extension().Equals(".zescene") && !path.ComponentAt(0).Equals("ZodiacEngine") && !path.ComponentAt(0).Equals("cache");
    }

    VFSResult<VFSPath> SceneSourcePathFromNative(std::string_view workspace, std::string_view native_path)
    {
        if (workspace.empty() || native_path.empty())
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        std::filesystem::path root_input = {}, source_input = {};
        if (!NativePathFromUtf8(workspace, root_input) || !NativePathFromUtf8(native_path, source_input) || !root_input.is_absolute() || !source_input.is_absolute() || !source_input.has_filename())
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        std::error_code error = {};
        const auto      root  = std::filesystem::canonical(root_input, error);
        if (error)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        const auto source = std::filesystem::weakly_canonical(source_input, error);
        if (error)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        auto                  source_part   = source.begin();
        std::filesystem::path source_prefix = {};
        for (auto root_part = root.begin(); root_part != root.end(); ++root_part)
        {
            if (source_part == source.end())
            {
                return VFSResult<VFSPath>::Fail(VFSError::PermissionDenied);
            }
            source_prefix /= *source_part;
            ++source_part;
        }
        // Compare directory identity, not folded path bytes. This handles both
        // case-insensitive macOS/Windows volumes and case-sensitive Windows folders.
        if (!std::filesystem::equivalent(root, source_prefix, error) || error)
        {
            return VFSResult<VFSPath>::Fail(VFSError::PermissionDenied);
        }
        std::filesystem::path relative = {};
        for (; source_part != source.end(); ++source_part)
        {
            relative /= *source_part;
        }
        if (relative.empty())
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        std::string virtual_path = {};
        if (!NativePathToUtf8(relative, true, virtual_path))
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        virtual_path.insert(0, 1, '/');
        const auto parsed = VFSPath::Parse(virtual_path.c_str());
        if (parsed.Failed() || !IsSceneSourcePath(parsed.Value()) || std::string_view(parsed.Value().CStr(), parsed.Value().Length()) != virtual_path)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        return parsed;
    }

    bool TrySceneDialogDirectory(std::string_view workspace, std::string_view scene_directory, const VFSPath& source, std::string* output)
    {
        if (!output || workspace.empty())
        {
            return false;
        }
        std::filesystem::path root = {}, directory = {};
        if (!NativePathFromUtf8(workspace, root) || !root.is_absolute())
        {
            return false;
        }
        if (source.IsValid())
        {
            if (!IsSceneSourcePath(source))
            {
                return false;
            }
            const auto parent = source.Parent();
            if (!NativePathFromUtf8({parent.CStr() + 1, parent.Length() - 1}, directory))
            {
                return false;
            }
        }
        else if (!NativePathFromUtf8(scene_directory, directory))
        {
            return false;
        }
        return NativePathToUtf8(directory.empty() ? root : root / directory, false, *output);
    }

    bool SaveEditorSceneDocument(EditorScene& owner, EditorSession& session, const SceneComponentSchemaRegistry& schemas, IVFSContext& vfs, const VFSPath& path, SceneDiagnostics* diagnostics)
    {
        if (!session.InMutationPhase() || owner.Session != &session || !session.Editable() || owner.SourceReadOnly || session.Active() || session.PendingRequestCount() != 0 || !IsSceneSourcePath(path))
        {
            SceneDiagnosticError(diagnostics, "Scene save requires an editable scene, valid project path and an idle exclusive mutation phase");
            return false;
        }

        std::string_view name = {};
        if (!TrySceneSourceName(owner.Name, &name))
        {
            SceneDiagnosticError(diagnostics, "Scene name exceeds the source document byte limit");
            return false;
        }
        SceneDocumentSettings settings = {.UUID = owner.UUID, .Name = std::string(name), .Grid = owner.AuthoredGrid, .Sky = owner.Sky};
        SceneDocument         document = {};
        if (!SceneDocument::Capture(*session.Scene(), settings, schemas, &document, diagnostics))
        {
            return false;
        }
        const auto saved = document.Save(vfs, path, diagnostics);
        if (saved.Failed())
        {
            return false;
        }

        // No callbacks or ECS mutation occur between capture and this checkpoint.
        // Keep the undo chain; saving is not itself a document edit.
        if (!session.MarkSaved())
        {
            SceneDiagnosticError(diagnostics, "Saved scene could not establish its editor checkpoint");
            return false;
        }
        owner.SourcePath = path;
        owner.MarkDirty(false);
        return true;
    }
} // namespace Tetragrama
