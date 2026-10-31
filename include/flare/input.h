#pragma once

#include <cstdint>

#include "flare/export.h"

namespace Flare {
    enum class Modifier : std::uint32_t {
        NONE = 0,
        ALT = 1u << 0,
        CONTROL = 1u << 1,
        SHIFT = 1u << 2,
        META = 1u << 3,
        CAPS_LOCK = 1u << 4,
        NUM_LOCK = 1u << 5
    };

    using Modifiers = std::uint32_t;

    [[nodiscard]] FLARE_API Modifiers operator|(Modifier left, Modifier right) noexcept;
    [[nodiscard]] FLARE_API Modifiers operator|(Modifiers left, Modifier right) noexcept;
    [[nodiscard]] FLARE_API bool HasModifier(Modifiers value, Modifier flag) noexcept;

    enum class MouseButton { NONE, LEFT, MIDDLE, RIGHT, EXTRA_1, EXTRA_2 };

    using MouseButtons = std::uint32_t;

    [[nodiscard]] FLARE_API MouseButtons MouseButtonMask(MouseButton button) noexcept;

    enum class MouseEventType { DOWN, UP, MOVE, DOUBLE_CLICK_EVENT };
}
