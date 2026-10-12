/**
 * logging.cpp - 日志系统实现
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
    // 创建控制台 sink
    console_sink_ = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink_->set_level(console_level);
    console_sink_->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    
    // 创建文件 sink (轮转日志，最大 10MB，保留 5 个)
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
        // 文件创建失败时仅使用控制台
        file_sink_.reset();
        file_error = e.what();
    }
    
    // 创建组合 logger
    std::vector<spdlog::sink_ptr> sinks = {console_sink_};
    if (file_sink_) sinks.push_back(file_sink_);
    
    logger_ = std::make_shared<spdlog::logger>("tensyoki", sinks.begin(), sinks.end());
    logger_->set_level(spdlog::level::trace);  // 捕获所有级别，由 sink 过滤
    logger_->flush_on(spdlog::level::warn);    // warn 及以上自动刷新
    
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
    if (!logger_) return;                      // 幂等
    logger_->flush();
    
    // 安全地替换默认 logger，避免重名
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