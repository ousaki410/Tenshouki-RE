/**
 * logging.cpp - logging system implementation
 */

#include "logging.hpp"
#include <filesystem>

namespace tensyoki {

std::shared_ptr<spdlog::logger> Logging::logger_;
std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> Logging::console_sink_;
std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> Logging::file_sink_;

void Logging::init(const std::string& log_file, 
                   spdlog::level::level_enum console_level,
                   spdlog::level::level_enum file_level) {
    // Create console sink
    console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink_->set_level(console_level);
    console_sink_->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    
    // Create file sink (rotating log, max 10MB, keep 5)
    std::string file_error;
    try {
        const auto parent = std::filesystem::path(log_file).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        file_sink_ = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 1024 * 1024 * 10, 5);
        file_sink_->set_level(file_level);
        file_sink_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
    } catch (const std::exception& e) {
        // Use console only if file creation fails
        file_sink_.reset();
        file_error = e.what();
    }
    
    // Create combined logger
    std::vector<spdlog::sink_ptr> sinks = {console_sink_};
    if (file_sink_) sinks.push_back(file_sink_);
    
    logger_ = std::make_shared<spdlog::logger>("tensyoki", sinks.begin(), sinks.end());
    logger_->set_level(spdlog::level::trace);  // capture all levels; filtered by sinks
    logger_->flush_on(spdlog::level::warn);    // auto-flush at warn and above
    
    spdlog::register_logger(logger_);
    spdlog::set_default_logger(logger_);
    
    if (!file_error.empty()) {
        SPDLOG_WARN("File logging disabled: {}", file_error);
    }
    
    SPDLOG_INFO("Logging initialized: console={}, file={}", 
                spdlog::level::to_string_view(console_level),
                file_sink_ ? spdlog::level::to_string_view(file_level) : "disabled");
}

void Logging::shutdown() {
    if (!logger_) return;                      // idempotent
    logger_->flush();
    
    // Safely replace the default logger to avoid name conflicts
    auto fallback = spdlog::get("tensyoki_fallback");
    if (!fallback) {
        fallback = spdlog::stderr_color_mt("tensyoki_fallback");
    }
    spdlog::set_default_logger(fallback);
    
    spdlog::drop("tensyoki");
    logger_.reset();
    console_sink_.reset();
    file_sink_.reset();
}

std::shared_ptr<spdlog::logger> Logging::get_logger(const std::string& name) {
    if (name == "tensyoki" || name.empty()) {
        return logger_ ? logger_ : spdlog::default_logger();
    }
    auto lg = spdlog::get(name);
    if (!lg) {
        lg = spdlog::get("tensyoki");
    }
    return lg ? lg : spdlog::default_logger();
}

void Logging::set_console_level(spdlog::level::level_enum level) {
    if (console_sink_) console_sink_->set_level(level);
}

void Logging::set_file_level(spdlog::level::level_enum level) {
    if (file_sink_) file_sink_->set_level(level);
}

void Logging::set_pattern(const std::string& pattern) {
    if (console_sink_) console_sink_->set_pattern(pattern);
    if (file_sink_) file_sink_->set_pattern(pattern);
    if (logger_) logger_->set_pattern(pattern);
}

void Logging::flush() {
    if (logger_) logger_->flush();
}

} // namespace tensyoki