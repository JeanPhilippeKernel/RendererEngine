#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <string_view>

namespace Tetragrama
{
    struct EditorScene;
    class EditorSession;

    /// Explicit native-path boundary for file dialogs. Resolve existing symlinks before
    /// accepting a destination; a project save must never escape its workspace.
    /// Native picker/configuration strings are bounded UTF-8, never Windows ANSI.
    [[nodiscard]] ZEngine::Core::VFS::VFSResult<ZEngine::Core::VFS::VFSPath> SceneSourcePathFromNative(std::string_view workspace, std::string_view native_path);
    [[nodiscard]] bool                                                       IsSceneSourcePath(const ZEngine::Core::VFS::VFSPath& path);

    /// UTF-8 picker suggestion only, without filesystem IO. A named source uses
    /// its parent; otherwise use the configured scene directory. Failure preserves
    /// output. Actual Open/Save still revalidate SceneSourcePathFromNative.
    [[nodiscard]] bool                                                       TrySceneDialogDirectory(std::string_view workspace, std::string_view scene_directory, const ZEngine::Core::VFS::VFSPath& source, std::string* output);

    /// Bounded C-string adapter shared by capture and native picker suggestions.
    [[nodiscard]] bool                                                       TrySceneSourceName(cstring name, std::string_view* out_name);

    /// Main-thread, exclusive-phase snapshot/save/checkpoint. Failure preserves the
    /// source path, history checkpoint, dirty state and previously saved file.
    /// Captures committed content; Editor::ProcessSceneSave drains editor-local settings first.
    [[nodiscard]] bool                                                       SaveEditorSceneDocument(EditorScene& owner, EditorSession& session, const ZEngine::ECS::SceneComponentSchemaRegistry& schemas, ZEngine::Core::VFS::IVFSContext& vfs, const ZEngine::Core::VFS::VFSPath& path, ZEngine::ECS::SceneDiagnostics* diagnostics);
} // namespace Tetragrama
