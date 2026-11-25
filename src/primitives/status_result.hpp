#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace elmasds {

/**
 * @brief Explicit domain error codes for emulation and storage operations.
 */
enum class ErrorCode {
    None = 0,
    InvalidArgument,
    FileNotFound,
    IoFailure,
    CorruptData,
    UnsupportedFormat,
    NotInitialized,
    PlatformFailure,
};

/**
 * @brief Structured error payload returned by fallible operations.
 */
struct StatusError {
    ErrorCode code = ErrorCode::None;
    std::string message;
};

/**
 * @brief Explicit result wrapper enforcing checked error handling without exceptions.
 * @tparam T Value type carried on success.
 */
template <typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }

    static Result err(ErrorCode code, std::string message) {
        return Result(StatusError{code, std::move(message)});
    }

    [[nodiscard]] bool is_ok() const noexcept { return std::holds_alternative<T>(storage_); }

    [[nodiscard]] bool is_err() const noexcept {
        return std::holds_alternative<StatusError>(storage_);
    }

    [[nodiscard]] const T& value() const& { return std::get<T>(storage_); }
    [[nodiscard]] T& value() & { return std::get<T>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(storage_)); }

    [[nodiscard]] const StatusError& error() const& { return std::get<StatusError>(storage_); }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(StatusError error) : storage_(std::move(error)) {}

    std::variant<T, StatusError> storage_;
};

/**
 * @brief Specialization-style void result carrying only optional failure details.
 */
class Status {
public:
    static Status ok() { return Status(std::nullopt); }

    static Status err(ErrorCode code, std::string message) {
        return Status(StatusError{code, std::move(message)});
    }

    [[nodiscard]] bool is_ok() const noexcept { return !error_.has_value(); }
    [[nodiscard]] bool is_err() const noexcept { return error_.has_value(); }
    [[nodiscard]] const StatusError& error() const& { return *error_; }

private:
    explicit Status(std::optional<StatusError> error) : error_(std::move(error)) {}

    std::optional<StatusError> error_;
};

}  // namespace elmasds
