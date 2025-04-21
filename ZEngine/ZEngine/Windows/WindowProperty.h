#pragma once
#include <Core/Containers/Strings.h>
#include <Core/CoreEvent.h>

namespace ZEngine::Windows
{
    struct WindowProperty
    {
    public:
        unsigned int                          Width{0}, Height{0};
        const char*                           Title{};
        bool                                  VSync{true};
        float                                 AspectRatio{0.0f};
        bool                                  IsMinimized{false};
        int                                   Dpi{0};
        float                                 DpiScale{0};
        std::function<void(Core::CoreEvent&)> CallbackFn;

    public:
        WindowProperty(unsigned int width = 1080, unsigned int height = 800, const char* title = "Engine Window") : Width(width), Height(height)
        {
            Title = title;
            UpdateAspectRatio();
        }

        ~WindowProperty() = default;

        void SetWidth(unsigned int w)
        {
            Width = w;
            UpdateAspectRatio();
        }

        void SetHeight(unsigned int h)
        {
            Height = h;
            UpdateAspectRatio();
        }

    private:
        void UpdateAspectRatio()
        {
            AspectRatio = (float) Width / (float) Height;
        }
    };
} // namespace ZEngine::Windows
