#include "test-framework.h"

#include <string>

#include "flare/json.h"

#include "core/json-utils.h"

void RunJsonTests(Test::Context& context) {
    context.Begin("json");
    context.Check(Flare::ValidateJsonTree(Flare::JSON({{"a", 1}, {"b", {1, 2, 3}}}), 8, 100), "shallow tree is valid");
    Flare::JSON deep = 1;
    for (int index = 0; index < 20; ++index) {
        deep = Flare::JSON::array({deep});
    }
    context.Check(!Flare::ValidateJsonTree(deep, 8, 1000), "deep tree is rejected");
    Flare::JSON wide = Flare::JSON::array();
    for (int index = 0; index < 20; ++index) {
        wide.push_back(index);
    }
    context.Check(!Flare::ValidateJsonTree(wide, 8, 10), "node limit is enforced");

    auto parsed = Flare::ParseJsonValue("{\"a\":[1,2,3]}", 1024, 8, 100);
    context.Check(parsed.IsOk() && parsed.Value()["a"].size() == 3, "valid json parsed");
    auto malformed = Flare::ParseJsonValue("{bad", 1024, 8, 100);
    context.Check(malformed.IsError() && malformed.GetError().code == Flare::ErrorCode::INVALID_ARGUMENT,
        "malformed json rejected");
    auto oversized = Flare::ParseJsonValue("1234567890", 4, 8, 100);
    context.Check(oversized.IsError(), "oversized json rejected");
    auto complex = Flare::ParseJsonValue("[[[[[[[[[[1]]]]]]]]]]", 1024, 3, 100);
    context.Check(complex.IsError(), "complex json rejected");

    const std::string dumped = Flare::SafeJsonDump(Flare::JSON{{"text", std::string("bad\x01value", 9)}});
    context.Check(dumped.find("text") != std::string::npos, "safe dump produces text");
}
