#include "flare/error.h"

namespace Flare {
    Error::Error() = default;

    Error::Error(ErrorCode errorCode, std::string errorMessage, std::string errorDetail)
        : code(errorCode),
          message(std::move(errorMessage)),
          detail(std::move(errorDetail)) {}

    Result<void>::Result() = default;

    Result<void>::Result(Error error) : failure(std::move(error)) {}

    bool Result<void>::IsOk() const noexcept {
        return failure.code == ErrorCode::NONE;
    }

    bool Result<void>::IsError() const noexcept {
        return failure.code != ErrorCode::NONE;
    }

    Result<void>::operator bool() const noexcept {
        return IsOk();
    }

    Error& Result<void>::GetError() & {
        return failure;
    }

    const Error& Result<void>::GetError() const& {
        return failure;
    }

    Result<void> Result<void>::Ok() {
        return Result();
    }

    Result<void> Result<void>::Fail(ErrorCode code, std::string message, std::string detail) {
        return Result(Error(code, std::move(message), std::move(detail)));
    }

    bool Error::IsOk() const noexcept {
        return code == ErrorCode::NONE;
    }

    std::string_view Error::CodeName() const noexcept {
        switch (code) {
            case ErrorCode::NONE:
                return "none";
            case ErrorCode::INVALID_ARGUMENT:
                return "invalid_argument";
            case ErrorCode::NOT_FOUND:
                return "not_found";
            case ErrorCode::ALREADY_EXISTS:
                return "already_exists";
            case ErrorCode::NOT_SUPPORTED:
                return "not_supported";
            case ErrorCode::PERMISSION_DENIED:
                return "permission_denied";
            case ErrorCode::TIMEOUT:
                return "timeout";
            case ErrorCode::CANCELLED:
                return "cancelled";
            case ErrorCode::BROWSER_NOT_READY:
                return "browser_not_ready";
            case ErrorCode::WINDOW_CLOSED:
                return "window_closed";
            case ErrorCode::WRONG_THREAD:
                return "wrong_thread";
            case ErrorCode::PLATFORM_FAILURE:
                return "platform_failure";
            case ErrorCode::IO_FAILURE:
                return "io_failure";
            case ErrorCode::BRIDGE_REJECTED:
                return "bridge_rejected";
            case ErrorCode::BRIDGE_RATE_LIMITED:
                return "bridge_rate_limited";
            case ErrorCode::NAVIGATION_BLOCKED:
                return "navigation_blocked";
            case ErrorCode::SINGLE_INSTANCE:
                return "single_instance";
            case ErrorCode::HANDLER_FAILED:
                return "handler_failed";
            case ErrorCode::UNKNOWN:
                break;
        }
        return "unknown";
    }

    std::string Error::ToString() const {
        std::string text = "[" + std::string(CodeName()) + "] " + message;
        if (!detail.empty()) {
            text += ": ";
            text += detail;
        }
        return text;
    }

    Error Error::Success() noexcept {
        return Error();
    }

    Error Error::Fail(ErrorCode errorCode, std::string errorMessage, std::string errorDetail) {
        return Error(errorCode, std::move(errorMessage), std::move(errorDetail));
    }
}
