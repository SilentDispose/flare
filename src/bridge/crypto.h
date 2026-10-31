#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Flare::Crypto {
    using Digest = std::array<std::uint8_t, 32>;

    Digest Sha256(std::span<const std::uint8_t> data);
    Digest Sha256(std::string_view text);
    Digest HmacSha256(std::string_view key, std::string_view message);
    std::string Base64UrlEncode(std::span<const std::uint8_t> data);
    std::optional<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view text);
    std::string ToHex(std::span<const std::uint8_t> data);
    bool IsHex(std::string_view text);
    std::vector<std::uint8_t> SystemRandomBytes(std::size_t count);
    std::string RandomToken(std::size_t bytes = 32);
    bool ConstantTimeEquals(std::string_view left, std::string_view right);
    std::string Sha256Hex(std::string_view text);
}
