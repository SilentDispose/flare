#include "test-framework.h"

#include <memory>
#include <string>

#include <flare/error.h>

void RunResultTests(Test::Context& context) {
    context.Begin("result");
    Flare::Result<int> value = 42;
    context.Check(value.IsOk(), "value result is ok");
    context.Check(value.Value() == 42, "value result holds the value");

    Flare::Result<int> failure = Flare::Error(Flare::ErrorCode::NOT_FOUND, "missing");
    context.Check(failure.IsError(), "error result reports failure");
    context.Check(failure.GetError().code == Flare::ErrorCode::NOT_FOUND, "error code is preserved");
    context.Check(failure.GetError().ToString().find("missing") != std::string::npos, "error message is preserved");

    Flare::Result<void> empty = Flare::Result<void>::Ok();
    context.Check(empty.IsOk(), "void result is ok");
    Flare::Result<void> emptyFailure = Flare::Result<void>::Fail(Flare::ErrorCode::TIMEOUT, "late");
    context.Check(emptyFailure.IsError(), "void failure is an error");
    context.Check(emptyFailure.GetError().CodeName() == "timeout", "error code name");

    auto pointer = std::make_unique<int>(7);
    Flare::Result<std::unique_ptr<int>> moveOnly = std::move(pointer);
    context.Check(moveOnly.IsOk() && *moveOnly.Value() == 7, "move-only result value");

    const Flare::Result<int> fallback = Flare::Error(Flare::ErrorCode::CANCELLED, "cancelled");
    context.Check(fallback.ValueOr(5) == 5, "ValueOr uses fallback");

    context.Check(Flare::Error::Success().IsOk(), "success helper");
    context.Check(
        Flare::Error::Fail(Flare::ErrorCode::IO_FAILURE, "io").code == Flare::ErrorCode::IO_FAILURE, "fail helper");
}
