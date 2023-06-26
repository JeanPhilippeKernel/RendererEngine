#pragma once
#include <Core/IInitializable.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/GraphicRendererInformation.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>

namespace ZEngine::Rendering::Renderers
{

    class GraphicRenderer : public Core::IInitializable
    {

    public:
        GraphicRenderer();
        virtual ~GraphicRenderer();
        virtual void StartScene(const Contracts::GraphicSceneLayout& scene_data);
        virtual void EndScene() = 0;

        virtual const Ref<GraphicRendererInformation>& GetRendererInformation() const;

        virtual void RequestOutputImageSize(uint32_t width = 1, uint32_t height = 1)
        {
            /*No Op*/
        }

        virtual Contracts::FramebufferViewLayout GetOutputImage(uint32_t frame_index) const
        {
            return {};
        }

    protected:
        uint32_t                               m_viewport_width{1};
        uint32_t                               m_viewport_height{1};
        Contracts::GraphicSceneLayout          m_scene_data;
        Ref<GraphicRendererInformation>        m_renderer_information;
    };
} // namespace ZEngine::Rendering::Renderers
