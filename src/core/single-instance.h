#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "flare/error.h"

namespace Flare {
    class SingleInstanceGuard {
    public:
        using Callback = std::function<void(std::vector<std::string>)>;

        static Result<std::unique_ptr<SingleInstanceGuard>> Create(
            std::string identifier, const std::vector<std::string>& arguments, Callback callback);
        ~SingleInstanceGuard();
        SingleInstanceGuard(const SingleInstanceGuard&) = delete;
        SingleInstanceGuard(SingleInstanceGuard&&) = delete;
        SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
        SingleInstanceGuard& operator=(SingleInstanceGuard&&) = delete;

    private:
        struct Impl;

        explicit SingleInstanceGuard(std::unique_ptr<Impl> guardImpl);

        std::unique_ptr<Impl> impl;
    };
}
