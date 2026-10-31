#pragma once

#include <cstdint>

#include "flare/export.h"

namespace Flare {
    struct Point {
        double x = 0.0;
        double y = 0.0;
    };

    struct Size {
        double width = 0.0;
        double height = 0.0;
    };

    struct FLARE_API Rect {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;

        [[nodiscard]] double Right() const noexcept;
        [[nodiscard]] double Bottom() const noexcept;
        [[nodiscard]] bool Contains(Point point) const noexcept;
        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] Rect Intersected(const Rect& other) const noexcept;
    };

    struct Insets {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
    };

    struct FLARE_API Color {
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 255;

        [[nodiscard]] static Color FromArgb(std::uint32_t argb) noexcept;
        [[nodiscard]] std::uint32_t ToArgb() const noexcept;
        [[nodiscard]] bool IsTransparent() const noexcept;
    };
}
