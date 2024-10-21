#pragma once
#include <Layers/Layer.h>
#include <ZEngineDef.h>
#include <vector>

namespace ZEngine::Windows::Layers
{
    class Layer;

    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(const Ref<Layer>& layer);
        void PushLayer(Ref<Layer>&& layer);

        void PushOverlayLayer(const Ref<Layer>& layer);
        void PushOverlayLayer(Ref<Layer>&& layer);

        void PopLayer(const Ref<Layer>& layer);
        void PopLayer(Ref<Layer>&& layer);

        void PopLayer();

        void PopOverlayLayer();

        void PopOverlayLayer(const Ref<Layer>& layer);
        void PopOverlayLayer(Ref<Layer>&& layer);

    public:
        std::vector<Ref<Layer>>::iterator begin()
        {
            return std::begin(m_layers);
        }
        std::vector<Ref<Layer>>::iterator end()
        {
            return std::end(m_layers);
        }

        std::vector<Ref<Layer>>::reverse_iterator rbegin()
        {
            return std::rbegin(m_layers);
        }
        std::vector<Ref<Layer>>::reverse_iterator rend()
        {
            return std::rend(m_layers);
        }

    private:
        std::vector<Ref<Layer>>           m_layers;
        std::vector<Ref<Layer>>::iterator current_it;
    };
} // namespace ZEngine::Windows::Layers
