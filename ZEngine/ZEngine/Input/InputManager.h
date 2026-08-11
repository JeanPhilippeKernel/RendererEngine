#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Input/InputTypes.h>

struct GLFWwindow;

namespace ZEngine::Input
{
    struct InputManager
    {
        void                    Initialize(Core::Memory::ArenaAllocator* arena, uint32_t max_actions = kMaxActions);
        void                    Dispose();

        // Registration — call before first Poll.
        // Returns slot index [0, max_actions). Asserts if capacity exceeded.
        [[nodiscard]] uint32_t  RegisterAction(const char* name, InputActionType type);

        // Binding — may be called at any time; takes effect on next Poll.
        void                    BindKey(uint32_t slot, int glfw_key, float scale = 1.0f);
        void                    BindMouseButton(uint32_t slot, int glfw_mouse_button, float scale = 1.0f);
        // Binds the scroll wheel Y axis. Accumulates between Poll calls.
        void                    BindScrollAxis(uint32_t slot, float scale = 1.0f);

        // Per-frame update — call once per frame on the main thread, before camera Update.
        void                    Poll(GLFWwindow* window);

        // Scroll accumulator — written by the GLFW scroll callback, drained by Poll.
        // Called from GameWindow's scroll callback trampoline.
        void                    AccumulateScroll(double yoffset);

        // Query — valid after Poll returns.
        const InputButtonState& GetButton(uint32_t slot) const;
        float                   GetAxis(uint32_t slot) const;
        Core::Maths::Vec2f      GetAxis2D(uint32_t slot) const;

        // Raw accessors — independent of the action map.
        Core::Maths::Vec2f      GetMouseDelta() const;    // logical pixels, this frame
        Core::Maths::Vec2f      GetMousePosition() const; // logical pixels, screen origin top-left
        float                   GetScrollDelta() const;   // scroll wheel Y this frame

        const InputFrame&       GetCurrentFrame() const;

    private:
        static uint32_t               FNV32(const char* str);

        Core::Memory::ArenaAllocator* m_arena                     = nullptr;
        InputAction*                  m_actions                   = nullptr;
        uint32_t                      m_max_actions               = 0;
        uint32_t                      m_action_count              = 0;

        InputFrame                    m_current                   = {};
        InputFrame                    m_prev                      = {};

        Core::Maths::Vec2f            m_mouse_pos                 = {};
        Core::Maths::Vec2f            m_last_mouse_pos            = {};
        Core::Maths::Vec2f            m_mouse_delta               = {};

        double                        m_scroll_accum              = 0.0;
        float                         m_scroll_delta              = 0.0f;

        // Per-slot scroll scale (non-zero only for slots bound with BindScrollAxis).
        float                         m_scroll_scale[kMaxActions] = {};

        bool                          m_first_poll                = true;
    };

} // namespace ZEngine::Input
