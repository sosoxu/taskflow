#pragma once

#include <string>
#include <variant>
#include <stdexcept>

namespace taskflow::common::result {

template <typename T>
class Result {
public:
    // 成功构造
    Result(T value) : data_(std::move(value)) {}

    // 失败构造
    static Result<T> failure(const std::string& error) {
        Result<T> r;
        r.data_ = Error{error};
        return r;
    }

    static Result<T> failure(std::string&& error) {
        Result<T> r;
        r.data_ = Error{std::move(error)};
        return r;
    }

    bool ok() const {
        return std::holds_alternative<T>(data_);
    }

    explicit operator bool() const {
        return ok();
    }

    const T& value() const& {
        if (!ok()) throw std::runtime_error(error());
        return std::get<T>(data_);
    }

    T& value() & {
        if (!ok()) throw std::runtime_error(error());
        return std::get<T>(data_);
    }

    T&& value() && {
        if (!ok()) throw std::runtime_error(error());
        return std::move(std::get<T>(data_));
    }

    const std::string& error() const {
        return std::get<Error>(data_).message;
    }

private:
    Result() = default;

    struct Error {
        std::string message;
    };

    std::variant<T, Error> data_;
};

// void 特化，用于无返回值操作
template <>
class Result<void> {
public:
    Result() : success_(true) {}

    static Result<void> failure(const std::string& error) {
        Result<void> r;
        r.success_ = false;
        r.error_ = error;
        return r;
    }

    static Result<void> failure(std::string&& error) {
        Result<void> r;
        r.success_ = false;
        r.error_ = std::move(error);
        return r;
    }

    bool ok() const { return success_; }
    explicit operator bool() const { return success_; }
    const std::string& error() const { return error_; }

private:
    bool success_ = false;
    std::string error_;
};

}  // namespace taskflow::common::result
