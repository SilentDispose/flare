#include "test-framework.h"

#include <cstdio>
#include <utility>

void Test::Context::Begin(std::string name) {
    current = std::move(name);
}

void Test::Context::Check(bool condition, std::string_view message, std::source_location location) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s: %s (%s:%u)\n", current.c_str(), std::string(message).c_str(), location.file_name(),
            static_cast<unsigned>(location.line()));
        failed.push_back(current + ": " + std::string(message));
    }
}

int Test::Context::Checks() const noexcept {
    return checks;
}

int Test::Context::Finish() const {
    if (failures == 0) {
        std::printf("All tests passed (%d checks)\n", checks);
        return 0;
    }
    std::printf("%d of %d checks failed\n", failures, checks);
    for (const std::string& entry : failed) {
        std::printf("  - %s\n", entry.c_str());
    }
    return 1;
}
