#pragma once
#include <ZEngine/UI/ZUIColorMath.h>
#include <array>
#include <cstdint>

namespace ZEngine::UI
{
    enum class ZUIColorModel : uint8_t
    {
        RGB,
        HSB,
        HSL
    };

    // Allocated lazily in PersistentArena, never shared between widgets/windows.
    struct ZUIColorNumberState
    {
        char  Buffer[16] = {};
        float PressX     = 0.f;
        int   PressValue = 0;
        bool  Editing    = false;
        bool  Dragging   = false;
        bool  Pressed    = false;
    };

    struct ZUIColorWidgetState
    {
        ZUIColorNumberState    Numbers[4]        = {};
        ZUIColorNumberState    Channels[4]       = {};
        ColorMath::RGBA        OldColor          = {};
        std::array<float, 4>   OldDisplay        = {};
        std::array<float, 4>   OldStored         = {};
        std::array<float, 4>   LastDisplay       = {};
        std::array<float, 4>   LastStored        = {};
        std::array<float, 4>   TransactionBefore = {};
        ColorMath::HSB         Hsb               = {};
        ColorMath::HSL         Hsl               = {};
        ColorMath::FieldOrigin Origin            = {};
        ColorMath::RGB         ChainStart        = {};
        ColorMath::Point       ChainPress        = {};
        ColorMath::HSB         HueOrigin         = {};
        int                    ChainTarget       = -1; // -1 field, 0..2 rings, 3..5 legs.
        char                   HexBuffer[32]     = {};
        uint64_t               DragKey           = 0;
        uint64_t               LastEditFrame     = 0;
        uint64_t               LastPickerFrame   = 0;
        uint64_t               LastStorageFrame  = 0;
        uintptr_t              BindingIdentity   = 0; // Compared only; never dereferenced.
        int                    Hue               = 0;
        ZUIColorModel          ChannelModel      = ZUIColorModel::RGB;
        bool                   Initialized       = false;
        bool                   HexEditing        = false;
        bool                   TransactionActive = false;
        bool                   RestoreOld        = false;
        bool                   BoundAlpha        = true;
        ZUIColorSpace          BoundSpace        = ZUIColorSpace::Srgb;
    };

    struct ZUIColorUndoEntry
    {
        uint64_t             Owner           = 0;
        std::array<float, 4> Before          = {};
        std::array<float, 4> After           = {};
        ZUIColorSpace        Space           = ZUIColorSpace::Srgb;
        bool                 Alpha           = true;
        uintptr_t            BindingIdentity = 0;
    };
} // namespace ZEngine::UI
