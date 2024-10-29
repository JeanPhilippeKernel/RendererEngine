#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Layers/Layer.h>
#include <vector>

namespace ZEngine::Windows::Layers
{
    class Layer;

    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        void PushLayer(const Helpers::Ref<Layer>& layer);
        void PushLayer(Helpers::Ref<Layer>&& layer);

        void PushOverlayLayer(const Helpers::Ref<Layer>& layer);
        void PushOverlayLayer(Helpers::Ref<Layer>&& layer);

        void PopLayer(const Helpers::Ref<Layer>& layer);
        void PopLayer(Helpers::Ref<Layer>&& layer);

        void PopLayer();

        void PopOverlayLayer();

        void PopOverlayLayer(const Helpers::Ref<Layer>& layer);
        void PopOverlayLayer(Helpers::Ref<Layer>&& layer);

    public:
        std::vector<Helpers::Ref<Layer>>::iterator begin()
        {
            return std::begin(m_layers);
        }
        std::vector<Helpers::Ref<Layer>>::iterator end()
        {
            return std::end(m_layers);
        }

        std::vector<Helpers::Ref<Layer>>::reverse_iterator rbegin()
        {
            return std::rbegin(m_layers);
        }
        std::vector<Helpers::Ref<Layer>>::reverse_iterator rend()
        {
            return std::rend(m_layers);
        }

    private:
        std::vector<Helpers::Ref<Layer>>           m_layers;
        std::vector<Helpers::Ref<Layer>>::iterator current_it;
    };
} // namespace ZEngine::Windows::Layers
