#include "bridge/crypto.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Flare::Crypto {
    namespace {
        constexpr std::array<std::uint32_t, 64> RoundConstants = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        constexpr std::array<std::uint32_t, 8> InitialHash = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

        constexpr char Base64UrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        std::uint32_t RotateRight(std::uint32_t value, std::uint32_t bits) {
            return (value >> bits) | (value << (32u - bits));
        }

        std::uint32_t ReadBigEndian(const std::uint8_t* data) {
            return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16)
                | (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
        }

        Digest Sha256Buffer(std::vector<std::uint8_t> message) {
            const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8u;
            message.push_back(0x80u);
            while (message.size() % 64u != 56u) {
                message.push_back(0u);
            }
            for (int shift = 56; shift >= 0; shift -= 8) {
                message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xFFu));
            }

            std::array<std::uint32_t, 8> hash = InitialHash;
            std::array<std::uint32_t, 64> schedule{};

            for (std::size_t offset = 0; offset < message.size(); offset += 64) {
                for (std::size_t index = 0; index < 16; ++index) {
                    schedule[index] = ReadBigEndian(message.data() + offset + index * 4);
                }
                for (std::size_t index = 16; index < 64; ++index) {
                    const std::uint32_t s0 = RotateRight(schedule[index - 15], 7)
                        ^ RotateRight(schedule[index - 15], 18) ^ (schedule[index - 15] >> 3);
                    const std::uint32_t s1 = RotateRight(schedule[index - 2], 17) ^ RotateRight(schedule[index - 2], 19)
                        ^ (schedule[index - 2] >> 10);
                    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
                }

                std::uint32_t a = hash[0];
                std::uint32_t b = hash[1];
                std::uint32_t c = hash[2];
                std::uint32_t d = hash[3];
                std::uint32_t e = hash[4];
                std::uint32_t f = hash[5];
                std::uint32_t g = hash[6];
                std::uint32_t h = hash[7];

                for (std::size_t index = 0; index < 64; ++index) {
                    const std::uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
                    const std::uint32_t choose = (e & f) ^ ((~e) & g);
                    const std::uint32_t temp1 = h + sum1 + choose + RoundConstants[index] + schedule[index];
                    const std::uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
                    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
                    const std::uint32_t temp2 = sum0 + majority;
                    h = g;
                    g = f;
                    f = e;
                    e = d + temp1;
                    d = c;
                    c = b;
                    b = a;
                    a = temp1 + temp2;
                }

                hash[0] += a;
                hash[1] += b;
                hash[2] += c;
                hash[3] += d;
                hash[4] += e;
                hash[5] += f;
                hash[6] += g;
                hash[7] += h;
            }

            Digest digest{};
            for (std::size_t index = 0; index < hash.size(); ++index) {
                digest[index * 4] = static_cast<std::uint8_t>((hash[index] >> 24) & 0xFFu);
                digest[index * 4 + 1] = static_cast<std::uint8_t>((hash[index] >> 16) & 0xFFu);
                digest[index * 4 + 2] = static_cast<std::uint8_t>((hash[index] >> 8) & 0xFFu);
                digest[index * 4 + 3] = static_cast<std::uint8_t>(hash[index] & 0xFFu);
            }
            return digest;
        }

        int Base64UrlValue(char character) {
            if (character >= 'A' && character <= 'Z') {
                return character - 'A';
            }
            if (character >= 'a' && character <= 'z') {
                return character - 'a' + 26;
            }
            if (character >= '0' && character <= '9') {
                return character - '0' + 52;
            }
            if (character == '-') {
                return 62;
            }
            if (character == '_') {
                return 63;
            }
            return -1;
        }
    }

    Digest Sha256(std::span<const std::uint8_t> data) {
        return Sha256Buffer(std::vector<std::uint8_t>(data.begin(), data.end()));
    }

    Digest Sha256(std::string_view text) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
        return Sha256Buffer(std::vector<std::uint8_t>(bytes, bytes + text.size()));
    }

    Digest HmacSha256(std::string_view key, std::string_view message) {
        std::array<std::uint8_t, 64> keyBlock{};
        if (key.size() > keyBlock.size()) {
            const Digest hashedKey = Sha256(key);
            std::copy(hashedKey.begin(), hashedKey.end(), keyBlock.begin());
        } else {
            std::copy(key.begin(), key.end(), keyBlock.begin());
        }

        std::array<std::uint8_t, 64> innerPad{};
        std::array<std::uint8_t, 64> outerPad{};
        for (std::size_t index = 0; index < keyBlock.size(); ++index) {
            innerPad[index] = static_cast<std::uint8_t>(keyBlock[index] ^ 0x36u);
            outerPad[index] = static_cast<std::uint8_t>(keyBlock[index] ^ 0x5cu);
        }

        std::vector<std::uint8_t> inner;
        inner.reserve(innerPad.size() + message.size());
        inner.insert(inner.end(), innerPad.begin(), innerPad.end());
        inner.insert(inner.end(), message.begin(), message.end());
        const Digest innerHash = Sha256Buffer(std::move(inner));

        std::vector<std::uint8_t> outer;
        outer.reserve(outerPad.size() + innerHash.size());
        outer.insert(outer.end(), outerPad.begin(), outerPad.end());
        outer.insert(outer.end(), innerHash.begin(), innerHash.end());
        return Sha256Buffer(std::move(outer));
    }

    std::string Base64UrlEncode(std::span<const std::uint8_t> data) {
        std::string encoded;
        encoded.reserve(((data.size() + 2) / 3) * 4);
        std::size_t index = 0;
        while (index + 3 <= data.size()) {
            const std::uint32_t value = (static_cast<std::uint32_t>(data[index]) << 16)
                | (static_cast<std::uint32_t>(data[index + 1]) << 8) | static_cast<std::uint32_t>(data[index + 2]);
            encoded.push_back(Base64UrlAlphabet[(value >> 18) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[(value >> 12) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[(value >> 6) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[value & 0x3Fu]);
            index += 3;
        }
        const std::size_t remaining = data.size() - index;
        if (remaining == 1) {
            const std::uint32_t value = static_cast<std::uint32_t>(data[index]) << 16;
            encoded.push_back(Base64UrlAlphabet[(value >> 18) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[(value >> 12) & 0x3Fu]);
        } else if (remaining == 2) {
            const std::uint32_t value = (static_cast<std::uint32_t>(data[index]) << 16)
                | (static_cast<std::uint32_t>(data[index + 1]) << 8);
            encoded.push_back(Base64UrlAlphabet[(value >> 18) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[(value >> 12) & 0x3Fu]);
            encoded.push_back(Base64UrlAlphabet[(value >> 6) & 0x3Fu]);
        }
        return encoded;
    }

    std::optional<std::vector<std::uint8_t>> Base64UrlDecode(std::string_view text) {
        if (text.size() % 4 == 1) {
            return std::nullopt;
        }
        std::size_t padding = 0;
        while (!text.empty() && text.back() == '=') {
            ++padding;
            text.remove_suffix(1);
        }
        if (padding > 2) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> decoded;
        decoded.reserve((text.size() * 3) / 4);

        std::uint32_t accumulator = 0;
        int bits = 0;
        for (char character : text) {
            const int value = Base64UrlValue(character);
            if (value < 0) {
                return std::nullopt;
            }
            accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                decoded.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xFFu));
            }
        }
        if (bits > 0 && (accumulator & ((1u << bits) - 1u)) != 0) {
            return std::nullopt;
        }
        return decoded;
    }

    std::string ToHex(std::span<const std::uint8_t> data) {
        constexpr char digits[] = "0123456789abcdef";
        std::string text;
        text.reserve(data.size() * 2);
        for (std::uint8_t byte : data) {
            text.push_back(digits[(byte >> 4) & 0x0Fu]);
            text.push_back(digits[byte & 0x0Fu]);
        }
        return text;
    }

    bool IsHex(std::string_view text) {
        if (text.empty() || text.size() % 2 != 0) {
            return false;
        }
        for (char character : text) {
            const bool digit = character >= '0' && character <= '9';
            const bool lower = character >= 'a' && character <= 'f';
            const bool upper = character >= 'A' && character <= 'F';
            if (!digit && !lower && !upper) {
                return false;
            }
        }
        return true;
    }

    std::string RandomToken(std::size_t bytes) {
        return Base64UrlEncode(SystemRandomBytes(bytes));
    }

    bool ConstantTimeEquals(std::string_view left, std::string_view right) {
        if (left.size() != right.size()) {
            return false;
        }
        std::uint8_t difference = 0;
        for (std::size_t index = 0; index < left.size(); ++index) {
            difference = static_cast<std::uint8_t>(
                difference | (static_cast<std::uint8_t>(left[index]) ^ static_cast<std::uint8_t>(right[index])));
        }
        return difference == 0;
    }

    std::string Sha256Hex(std::string_view text) {
        const Digest digest = Sha256(text);
        return ToHex(digest);
    }
}
