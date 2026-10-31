#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <sys/random.h>
#include <unistd.h>
#include <vector>

#include "bridge/crypto.h"

namespace Flare::Crypto {
    std::vector<std::uint8_t> SystemRandomBytes(std::size_t count) {
        std::vector<std::uint8_t> bytes(count);
        std::size_t offset = 0;
        while (offset < count) {
            const ssize_t received = getrandom(bytes.data() + offset, count - offset, 0);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            offset += static_cast<std::size_t>(received);
        }
        if (offset == count) {
            return bytes;
        }
        FILE* source = fopen("/dev/urandom", "rb");
        if (!source) {
            throw std::runtime_error("unable to open /dev/urandom");
        }
        const std::size_t readCount = fread(bytes.data(), 1, count, source);
        fclose(source);
        if (readCount != count) {
            throw std::runtime_error("unable to read entropy");
        }
        return bytes;
    }
}
