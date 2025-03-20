// #pragma once
// #include <Helpers/IntrusivePtr.h>
// #include <Core/Container/Array.h>
// #include <Layers/Layer.h>
//
// namespace ZEngine::Windows::Layers
//{
//     class Layer;
//
//     struct LayerStack
//     {
//         template <typename T>
//         using Array  = Core::Container::Array<T>;
//
//         LayerStack() = default;
//         ~LayerStack();
//
//         void                            PushLayer(const Helpers::Ref<Layer>& layer);
//         void                            PushLayer(Helpers::Ref<Layer>&& layer);
//
//         void                            PushOverlayLayer(const Helpers::Ref<Layer>& layer);
//         void                            PushOverlayLayer(Helpers::Ref<Layer>&& layer);
//
//         void                            PopLayer(const Helpers::Ref<Layer>& layer);
//         void                            PopLayer(Helpers::Ref<Layer>&& layer);
//
//         void                            PopLayer();
//
//         void                            PopOverlayLayer();
//
//         void                            PopOverlayLayer(const Helpers::Ref<Layer>& layer);
//         void                            PopOverlayLayer(Helpers::Ref<Layer>&& layer);
//
//         Array<ZRawPtr(Layer)>::iterator begin()
//         {
//             return std::begin(m_layers);
//         }
//
//         Array<ZRawPtr(Layer)>::iterator end()
//         {
//             return std::end(m_layers);
//         }
//
//     private:
//         Array<ZRawPtr(Layer)>           m_layers;
//         Array<ZRawPtr(Layer)>::iterator m_current_it;
//     };
// } // namespace ZEngine::Windows::Layers
