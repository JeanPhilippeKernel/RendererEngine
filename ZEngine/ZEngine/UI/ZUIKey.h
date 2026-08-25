#pragma once
#include <cstdint>

namespace ZEngine::UI
{
    // Platform-agnostic key codes — consumers map OS keys to these.
    // Covers keyboard input needed by UI widgets.
    enum class ZUIKey : uint32_t
    {
        None = 0,

        // Printable (A-Z)
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Digits (top row)
        D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,

        // Function
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

        // Navigation
        Left, Right, Up, Down,
        Home, End,
        PageUp, PageDown,
        Tab,

        // Editing
        Enter,
        Backspace,
        Delete,
        Insert,
        Escape,
        Space,

        // Modifiers (used as keys, not just flags)
        LeftCtrl,  RightCtrl,
        LeftShift, RightShift,
        LeftAlt,   RightAlt,

        // Misc
        CapsLock,
        Grave,       // `~
        Minus,       // -_
        Equal,       // =+
        LeftBracket, // [{
        RightBracket,// ]}
        Backslash,   // \|
        Semicolon,   // ;:
        Apostrophe,  // '"
        Comma,       // ,<
        Period,      // .>
        Slash,       // /?

        Count
    };

} // namespace ZEngine::UI
