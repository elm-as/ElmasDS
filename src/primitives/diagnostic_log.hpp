#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace elmasds {

/**
 * @brief Severity level for structured emulator diagnostics.
 */
enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
};

/**
 * @brief Immutable structured diagnostic entry emitted by subsystems.
 */
struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string subsystem;
    std::string message;
};

/**
 * @brief Instance-owned structured logger replacing mutable global logging state.
 */
class DiagnosticLog {
public:
    using SinkCallback = std::function<void(const LogEntry&)>;

    explicit DiagnosticLog(LogLevel minimum_level = LogLevel::Info);

    void set_minimum_level(LogLevel minimum_level) noexcept;
    void set_sink(SinkCallback sink);

    void log(LogLevel level, std::string_view subsystem, std::string_view message);
    void info(std::string_view subsystem, std::string_view message);
    void warning(std::string_view subsystem, std::string_view message);
    void error(std::string_view subsystem, std::string_view message);

    [[nodiscard]] const std::vector<LogEntry>& entries() const noexcept;
    void clear() noexcept;

private:
    LogLevel minimum_level_;
    SinkCallback sink_;
    std::vector<LogEntry> entries_;
};

}  // namespace elmasds
