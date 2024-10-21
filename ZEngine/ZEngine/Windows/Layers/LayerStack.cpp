#include <pch.h>
#include <LayerStack.h>

namespace ZEngine::Windows::Layers
{

    LayerStack::~LayerStack()
    {
        for (auto& layer : m_layers)
        {
            layer.reset();
        }
        m_layers.clear();
        m_layers.shrink_to_fit();
    }

    void LayerStack::PushLayer(const Ref<Layer>& layer)
    {
        if (m_layers.empty())
        {
            m_layers.push_back(layer);
            current_it = std::begin(m_layers);
        }
        else
        {
            current_it = m_layers.emplace(current_it, layer);
        }
    }

    void LayerStack::PushLayer(Ref<Layer>&& layer)
    {
        if (m_layers.empty())
        {
            m_layers.push_back(layer);
            current_it = std::begin(m_layers);
        }
        else
        {
            current_it = m_layers.emplace(current_it, layer);
        }
    }

    void LayerStack::PushOverlayLayer(const Ref<Layer>& layer)
    {
        m_layers.push_back(layer);

        if (m_layers.size() == 1)
        {
            current_it = std::begin(m_layers);
        }
    }

    void LayerStack::PushOverlayLayer(Ref<Layer>&& layer)
    {
        m_layers.push_back(layer);

        if (m_layers.size() == 1)
        {
            current_it = std::begin(m_layers);
        }
    }

    void LayerStack::PopLayer(const Ref<Layer>& layer)
    {
        const auto it = std::find_if(std::begin(m_layers), std::end(m_layers), [&layer](const Ref<Layer>& x) {
            return x == layer;
        });

        if (it != std::end(m_layers))
        {
            current_it = m_layers.erase(it);
        }
    }

    void LayerStack::PopLayer(Ref<Layer>&& layer)
    {
        const auto it = std::find_if(std::begin(m_layers), std::end(m_layers), [&layer](const Ref<Layer>& x) {
            return x == layer;
        });

        if (it != std::end(m_layers))
        {
            current_it = m_layers.erase(it);
        }
    }

    void LayerStack::PopLayer()
    {
        const auto it = std::begin(m_layers);
        if (it != std::end(m_layers))
        {
            current_it = m_layers.erase(it);
        }
    }

    void LayerStack::PopOverlayLayer()
    {
        const auto it = std::prev(std::end(m_layers));
        if (it != std::end(m_layers))
        {
            m_layers.erase(it);
        }
    }

    void LayerStack::PopOverlayLayer(const Ref<Layer>& layer)
    {
        const auto it = std::find_if(std::begin(m_layers), std::end(m_layers), [&layer](const Ref<Layer>& x) {
            return x == layer;
        });

        if (it != std::end(m_layers))
        {
            m_layers.erase(it);
        }
    }

    void LayerStack::PopOverlayLayer(Ref<Layer>&& layer)
    {
        const auto it = std::find_if(std::begin(m_layers), std::end(m_layers), [&layer](const Ref<Layer>& x) {
            return x == layer;
        });

        if (it != std::end(m_layers))
        {
            m_layers.erase(it);
        }
    }
} // namespace ZEngine::Windows::Layers
