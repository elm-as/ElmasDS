#include "primitives/diagnostic_log.hpp"

#include <utility>

namespace elmasds {

DiagnosticLog::DiagnosticLog(LogLevel minimum_level) : minimum_level_(minimum_level) {}

void DiagnosticLog::set_minimum_level(LogLevel minimum_level) noexcept {
    minimum_level_ = minimum_level;
}

void DiagnosticLog::set_sink(SinkCallback sink) {
    sink_ = std::move(sink);
}

void DiagnosticLog::log(LogLevel level, std::string_view subsystem, std::string_view message) {
    if (static_cast<int>(level) < static_cast<int>(minimum_level_)) {
        return;
    }

    LogEntry entry{level, std::string(subsystem), std::string(message)};
    entries_.push_back(entry);
    if (sink_) {
        sink_(entry);
    }
}

void DiagnosticLog::info(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Info, subsystem, message);
}

void DiagnosticLog::warning(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Warning, subsystem, message);
}

void DiagnosticLog::error(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Error, subsystem, message);
}

const std::vector<LogEntry>& DiagnosticLog::entries() const noexcept {
    return entries_;
}

void DiagnosticLog::clear() noexcept {
    entries_.clear();
}

}  // namespace elmasds
