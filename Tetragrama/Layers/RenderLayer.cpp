#include <pch.h>
#include <Editor.h>
#include <MessageToken.h>
#include <Messengers/Messenger.h>
#include <RenderLayer.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
using namespace Tetragrama::Controllers;

using namespace ZEngine;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Windows;
using namespace ZEngine::Core;
using namespace ZEngine::Helpers;

namespace Tetragrama::Layers
{

    RenderLayer::RenderLayer(std::string_view name) : Layer(name) {}

    RenderLayer::~RenderLayer() {}

    void RenderLayer::Initialize() {}

    void RenderLayer::Deinitialize() {}

    void RenderLayer::Update(TimeStep dt)
    {
        if (ParentContext)
        {
            auto ctx = reinterpret_cast<EditorContext*>(ParentContext);
            ctx->CurrentScenePtr->RenderScene->ComputeAllTransforms();
            ctx->CameraControllerPtr->Update(dt);
        }
    }

    bool RenderLayer::OnEvent(CoreEvent& e)
    {
        if (ParentContext)
        {
            auto ctx = reinterpret_cast<EditorContext*>(ParentContext);
            ctx->CameraControllerPtr->OnEvent(e);
        }
        return false;
    }

    void RenderLayer::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        if (!ParentContext)
        {
            return;
        }

        auto ctx    = reinterpret_cast<EditorContext*>(ParentContext);
        auto camera = ctx->CameraControllerPtr->GetCamera();
        auto data   = ctx->CurrentScenePtr->RenderScene->GetRawData();
        renderer->DrawScene(command_buffer, camera.get(), data.get());
    }
} // namespace Tetragrama::Layers
