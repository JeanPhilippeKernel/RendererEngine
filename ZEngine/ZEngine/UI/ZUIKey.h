#pragma once
#include <cstdint>

namespace ZEngine::UI
{
    /// @brief Platform-agnostic key codes — consumers map OS keys to these.
    ///
    /// Covers all keyboard input needed by UI widgets (text editing, navigation,
    /// modifier keys). Map platform-specific key constants to ZUIKey in
    /// ZUIFeedKey before passing to the ZUI input feed.
    enum class ZUIKey : uint32_t
    {
        None = 0, ///< No key / invalid.

        // Printable (A-Z)
        A  =  1, ///< Letter key 'A'.
        B  =  2, ///< Letter key 'B'.
        C  =  3, ///< Letter key 'C'.
        D  =  4, ///< Letter key 'D'.
        E  =  5, ///< Letter key 'E'.
        F  =  6, ///< Letter key 'F'.
        G  =  7, ///< Letter key 'G'.
        H  =  8, ///< Letter key 'H'.
        I  =  9, ///< Letter key 'I'.
        J  = 10, ///< Letter key 'J'.
        K  = 11, ///< Letter key 'K'.
        L  = 12, ///< Letter key 'L'.
        M  = 13, ///< Letter key 'M'.
        N  = 14, ///< Letter key 'N'.
        O  = 15, ///< Letter key 'O'.
        P  = 16, ///< Letter key 'P'.
        Q  = 17, ///< Letter key 'Q'.
        R  = 18, ///< Letter key 'R'.
        S  = 19, ///< Letter key 'S'.
        T  = 20, ///< Letter key 'T'.
        U  = 21, ///< Letter key 'U'.
        V  = 22, ///< Letter key 'V'.
        W  = 23, ///< Letter key 'W'.
        X  = 24, ///< Letter key 'X'.
        Y  = 25, ///< Letter key 'Y'.
        Z  = 26, ///< Letter key 'Z'.

        // Digits (top row)
        D0 = 27, ///< Digit key '0'.
        D1 = 28, ///< Digit key '1'.
        D2 = 29, ///< Digit key '2'.
        D3 = 30, ///< Digit key '3'.
        D4 = 31, ///< Digit key '4'.
        D5 = 32, ///< Digit key '5'.
        D6 = 33, ///< Digit key '6'.
        D7 = 34, ///< Digit key '7'.
        D8 = 35, ///< Digit key '8'.
        D9 = 36, ///< Digit key '9'.

        // Function
        F1  = 37, ///< Function key F1.
        F2  = 38, ///< Function key F2.
        F3  = 39, ///< Function key F3.
        F4  = 40, ///< Function key F4.
        F5  = 41, ///< Function key F5.
        F6  = 42, ///< Function key F6.
        F7  = 43, ///< Function key F7.
        F8  = 44, ///< Function key F8.
        F9  = 45, ///< Function key F9.
        F10 = 46, ///< Function key F10.
        F11 = 47, ///< Function key F11.
        F12 = 48, ///< Function key F12.

        // Navigation
        Left     = 49, ///< Left arrow
        Right    = 50, ///< Right arrow
        Up       = 51, ///< Up arrow
        Down     = 52, ///< Down arrow
        Home     = 53, ///< Home key — cursor to start of line
        End      = 54, ///< End key — cursor to end of line
        PageUp   = 55, ///< Page Up key.
        PageDown = 56, ///< Page Down key.
        Tab      = 57, ///< Tab key.

        // Editing
        Enter     = 58, ///< Confirm / commit text field
        Backspace = 59, ///< Delete preceding character
        Delete    = 60, ///< Forward-delete at cursor
        Insert    = 61, ///< Insert key.
        Escape    = 62, ///< Cancel / clear focus
        Space     = 63, ///< Activate focused button

        // Modifiers (used as keys, not just flags)
        LeftCtrl  = 64, RightCtrl  = 65,
        LeftShift = 66, RightShift = 67,
        LeftAlt   = 68, RightAlt   = 69,

        // Misc
        CapsLock     = 70, ///< Caps Lock key.
        Grave        = 71, ///< `~
        Minus        = 72, ///< -_
        Equal        = 73, ///< =+
        LeftBracket  = 74, ///< [{
        RightBracket = 75, ///< ]}
        Backslash    = 76, ///< \|
        Semicolon    = 77, ///< ;:
        Apostrophe   = 78, ///< '"
        Comma        = 79, ///< ,<
        Period       = 80, ///< .>
        Slash        = 81, ///< /?

        Count = 82 ///< Total number of key codes; not a valid key.
    };

} // namespace ZEngine::UI
