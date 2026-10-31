#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "flare/export.h"

namespace Flare {
    enum class ErrorCode {
        NONE,
        INVALID_ARGUMENT,
        NOT_FOUND,
        ALREADY_EXISTS,
        NOT_SUPPORTED,
        PERMISSION_DENIED,
        TIMEOUT,
        CANCELLED,
        BROWSER_NOT_READY,
        WINDOW_CLOSED,
        WRONG_THREAD,
        PLATFORM_FAILURE,
        IO_FAILURE,
        BRIDGE_REJECTED,
        BRIDGE_RATE_LIMITED,
        NAVIGATION_BLOCKED,
        SINGLE_INSTANCE,
        HANDLER_FAILED,
        UNKNOWN
    };

    struct FLARE_API Error {
        ErrorCode code = ErrorCode::NONE;
        std::string message;
        std::string detail;

        Error();
        Error(ErrorCode errorCode, std::string errorMessage, std::string errorDetail = {});

        [[nodiscard]] bool IsOk() const noexcept;
        [[nodiscard]] std::string_view CodeName() const noexcept;
        [[nodiscard]] std::string ToString() const;

        static Error Success() noexcept;
        static Error Fail(ErrorCode errorCode, std::string errorMessage, std::string errorDetail = {});
    };

    template <typename T>
    class Result {
    public:
        Result(T value) : storage(std::move(value)) {}

        Result(Error error) : storage(std::move(error)) {}

        Result(const Result&) = default;
        Result(Result&&) noexcept = default;
        Result& operator=(const Result&) = default;
        Result& operator=(Result&&) noexcept = default;

        [[nodiscard]] bool IsOk() const noexcept { return storage.index() == 0; }

        [[nodiscard]] bool IsError() const noexcept { return storage.index() == 1; }

        explicit operator bool() const noexcept { return IsOk(); }

        T& Value() & { return *std::get_if<0>(&storage); }

        const T& Value() const& { return *std::get_if<0>(&storage); }

        T&& Value() && { return std::move(*std::get_if<0>(&storage)); }

        T ValueOr(T fallback) const {
            const T* value = std::get_if<0>(&storage);
            return value ? *value : std::move(fallback);
        }

        Error& GetError() & { return *std::get_if<1>(&storage); }

        const Error& GetError() const& { return *std::get_if<1>(&storage); }

        T* operator->() { return std::get_if<0>(&storage); }

        const T* operator->() const { return std::get_if<0>(&storage); }

        T& operator*() { return Value(); }

        const T& operator*() const { return Value(); }

        static Result Ok(T value) { return Result(std::move(value)); }

        static Result Fail(ErrorCode code, std::string message, std::string detail = {}) {
            return Result(Error(code, std::move(message), std::move(detail)));
        }

    private:
        std::variant<T, Error> storage;
    };

    template <>
    class FLARE_API Result<void> {
    public:
        Result();
        Result(Error error);

        [[nodiscard]] bool IsOk() const noexcept;
        [[nodiscard]] bool IsError() const noexcept;
        explicit operator bool() const noexcept;

        Error& GetError() &;
        const Error& GetError() const&;

        static Result Ok();
        static Result Fail(ErrorCode code, std::string message, std::string detail = {});

    private:
        Error failure;
    };

    template <typename T>
    using ResultCallback = std::function<void(Result<T>)>;

    using CompletionCallback = std::function<void(Result<void>)>;
    using ErrorCallback = std::function<void(const Error&)>;
}
