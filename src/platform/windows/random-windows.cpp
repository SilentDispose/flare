#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "bridge/crypto.h"

namespace Flare::Crypto {
    std::vector<std::uint8_t> SystemRandomBytes(std::size_t count) {
        std::vector<std::uint8_t> bytes(count);
        std::size_t offset = 0;
        while (offset < count) {
            const std::size_t chunk = std::min<std::size_t>(count - offset, 0x40000000u);
            const NTSTATUS status = BCryptGenRandom(
                nullptr, bytes.data() + offset, static_cast<ULONG>(chunk), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status != 0) {
                throw std::runtime_error("BCryptGenRandom failed");
            }
            offset += chunk;
        }
        return bytes;
    }
}
