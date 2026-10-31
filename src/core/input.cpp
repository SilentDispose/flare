#include "flare/input.h"

namespace Flare {
    Modifiers operator|(Modifier left, Modifier right) noexcept {
        return static_cast<Modifiers>(left) | static_cast<Modifiers>(right);
    }

    Modifiers operator|(Modifiers left, Modifier right) noexcept {
        return left | static_cast<Modifiers>(right);
    }

    bool HasModifier(Modifiers value, Modifier flag) noexcept {
        return (value & static_cast<Modifiers>(flag)) != 0;
    }

    MouseButtons MouseButtonMask(MouseButton button) noexcept {
        switch (button) {
            case MouseButton::LEFT:
                return 1u << 0;
            case MouseButton::RIGHT:
                return 1u << 1;
            case MouseButton::MIDDLE:
                return 1u << 2;
            case MouseButton::EXTRA_1:
                return 1u << 3;
            case MouseButton::EXTRA_2:
                return 1u << 4;
            case MouseButton::NONE:
                break;
        }
        return 0;
    }
}
