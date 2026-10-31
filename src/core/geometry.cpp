#include "flare/geometry.h"

#include <algorithm>

namespace Flare {
    double Rect::Right() const noexcept {
        return x + width;
    }

    double Rect::Bottom() const noexcept {
        return y + height;
    }

    bool Rect::Contains(Point point) const noexcept {
        return point.x >= x && point.x < Right() && point.y >= y && point.y < Bottom();
    }

    bool Rect::IsEmpty() const noexcept {
        return width <= 0.0 || height <= 0.0;
    }

    Rect Rect::Intersected(const Rect& other) const noexcept {
        const double left = std::max(x, other.x);
        const double top = std::max(y, other.y);
        const double right = std::min(Right(), other.Right());
        const double bottom = std::min(Bottom(), other.Bottom());
        if (right <= left || bottom <= top) {
            return Rect{};
        }
        return Rect{left, top, right - left, bottom - top};
    }

    Color Color::FromArgb(std::uint32_t argb) noexcept {
        return Color{static_cast<std::uint8_t>((argb >> 16) & 0xFFu), static_cast<std::uint8_t>((argb >> 8) & 0xFFu),
            static_cast<std::uint8_t>(argb & 0xFFu), static_cast<std::uint8_t>((argb >> 24) & 0xFFu)};
    }

    std::uint32_t Color::ToArgb() const noexcept {
        return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16)
            | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
    }

    bool Color::IsTransparent() const noexcept {
        return a < 255;
    }
}
