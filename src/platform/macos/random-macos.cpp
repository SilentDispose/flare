#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "bridge/crypto.h"

namespace Flare::Crypto {
    std::vector<std::uint8_t> SystemRandomBytes(std::size_t count) {
        std::vector<std::uint8_t> bytes(count);
        if (count > 0) {
            arc4random_buf(bytes.data(), count);
        }
        return bytes;
    }
}
