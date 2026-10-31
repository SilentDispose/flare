#pragma once

#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace Test {
    class Context {
    public:
        void Begin(std::string name);
        void Check(
            bool condition, std::string_view message, std::source_location location = std::source_location::current());

        template <typename T>
        void Equal(const T& actual, const T& expected, std::string_view message,
            std::source_location location = std::source_location::current()) {
            Check(actual == expected, message, location);
        }

        [[nodiscard]] int Checks() const noexcept;
        int Finish() const;

    private:
        std::string current;
        int checks = 0;
        int failures = 0;
        std::vector<std::string> failed;
    };
}

void RunResultTests(Test::Context& context);
void RunEventTests(Test::Context& context);
void RunCryptoTests(Test::Context& context);
void RunUrlTests(Test::Context& context);
void RunJsonTests(Test::Context& context);
void RunOptionTests(Test::Context& context);
void RunEmbeddedWebTests(Test::Context& context);
void RunProtocolTests(Test::Context& context);
void RunBridgeServerTests(Test::Context& context);
