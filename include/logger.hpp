#pragma once

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/StreamSink.h>

#include <string>
#include <mutex>
#include <stdexcept>
#include <filesystem>

#include "EventBus.hpp"
#include "types.hpp"
#include "utils.hpp"

#define CPU_PIN 8

class Logger {
private:
    quill::Logger* logger_;
    static std::string filename_;
    static std::mutex init_mutex_;
    static bool is_initialized_;

    Logger() {
        if (filename_.empty()) {
            throw std::runtime_error("Logger filename is empty");
        }
        try {
            std::filesystem::path file_path(filename_);
            if (auto parent = file_path.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent);
            }

            if (!quill::Backend::is_running()) {
                quill::BackendOptions backend_options;
                backend_options.cpu_affinity = CPU_PIN;
                quill::Backend::start(backend_options);
            }

            // File sink
            quill::FileSinkConfig file_cfg;
            file_cfg.set_open_mode('a');
            file_cfg.set_filename_append_option(quill::FilenameAppendOption::StartDateTime);

            quill::PatternFormatterOptions formatter_options;
            formatter_options.format_pattern = "%(time) [%(log_level)] %(file_name): %(message)";
            formatter_options.timestamp_pattern = "%Y-%m-%d %H:%M:%S.%Qus";
            formatter_options.timestamp_timezone = quill::Timezone::LocalTime;
            file_cfg.set_override_pattern_formatter_options(formatter_options);

            auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(
                filename_, file_cfg, quill::FileEventNotifier{}
            );

            // if(enable_console_log_){
            //     auto console_sink = quill::Frontend::create_or_get_sink<quill::StreamSink>(        
            //         "stdout",
            //         std::filesystem::path{}, 
            //         stdout);

            //     // Create logger with both sinks
            //     logger_ = quill::Frontend::create_or_get_logger("market_logger", {file_sink, console_sink});
            // }
            // else{
            //     logger_ = quill::Frontend::create_or_get_logger("market_logger", {file_sink});
            // }
            
            logger_ = quill::Frontend::create_or_get_logger("market_logger", {file_sink});

            logger_->set_log_level(quill::LogLevel::Info);

        } catch (const std::exception& e) {
            throw std::runtime_error("Logger initialization failed: " + std::string(e.what()));
        }
    }

public:
    // --- Initialization ---
    static void init(const std::string& custom_filename) {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (is_initialized_) {
            throw std::runtime_error("Logger already initialized");
        }
        if (custom_filename.empty()) {
            throw std::invalid_argument("Custom filename cannot be empty");
        }
        filename_ = custom_filename;
        is_initialized_ = true;

    }

    static Logger& getInstance() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (!is_initialized_) {
            throw std::runtime_error("Logger not initialized. Call Logger::init first.");
        }
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    inline void logInfo(const std::string& msg) { LOG_INFO(logger_, "{}", msg); }
    inline void logWarn(const std::string& msg) { LOG_WARNING(logger_, "{}", msg); }
    inline void logError(const std::string& msg) { LOG_ERROR(logger_, "{}", msg); }
    inline void logDebug(const std::string& msg) { LOG_DEBUG(logger_, "{}", msg); }

    quill::Logger* getQuillLogger() { return logger_; }

    inline void logTradeEvent(const TradeEvent& event) {
        auto elapsed = get_time_now_nano() - event.data.trade_time;
        LOG_INFO(logger_, "TradeEvent: source={}, symbol={}, price={:.6f}, quantity={:.4f}, trade_time={}, elapsed={}",
            event.data.source, event.data.symbol, event.data.price, event.data.quantity,
            event.data.trade_time, elapsed);
    }

    inline void logCandleStickDataEvent(const CandleStickDataEvent& event) {
        LOG_INFO(logger_, "CandleStickDataEvent: source={}, symbol={}, interval={}, close_time={}, open_time={}, "
            "close={:.6f}, open={:.6f}, high={:.6f}, low={:.6f}, volume={:.4f}, trade_count={}",
            event.data.source, event.data.symbol, event.data.interval, event.data.close_time, event.data.open_time,
            event.data.close, event.data.open, event.data.high, event.data.low, event.data.volume,
            event.data.trade_count);
    }

    inline void logTickerDataEvent(const TickerDataEvent& event) {
        auto elapsed = get_time_now_nano() - event.data.timestamp;
        LOG_INFO(logger_, "TickerDataEvent: source={}, symbol={}, best_ask={:.6f}, best_bid={:.6f}, "
            "high_24h={:.6f}, low_24h={:.6f}, last_price={:.6f}, price_change_24h={:.6f}, elapsed={}",
            event.data.source, event.data.symbol, event.data.best_ask, event.data.best_bid, event.data.high_24h,
            event.data.low_24h, event.data.last_price, event.data.price_change_24h, elapsed);
    }

    inline void logOrderBookDataEvent(const OrderBookDataEvent& event) {
        auto elapsed = get_time_now_nano() - event.data.timestamp;
        LOG_INFO(logger_, "OrderBookDataEvent: source={}, symbol={}, timestamp={}, elapsed={}",
            event.data.source, event.data.symbol, event.data.timestamp, elapsed);
    }

    void subscribeToBus(std::shared_ptr<EventBus> event_bus) {
        event_bus->subscribe<TradeEvent>([this](const TradeEvent& e) { this->logTradeEvent(e); });
        event_bus->subscribe<CandleStickDataEvent>([this](const CandleStickDataEvent& e) { this->logCandleStickDataEvent(e); });
        event_bus->subscribe<TickerDataEvent>([this](const TickerDataEvent& e) { this->logTickerDataEvent(e); });
        event_bus->subscribe<OrderBookDataEvent>([this](const OrderBookDataEvent& e) { this->logOrderBookDataEvent(e); });
    }

    void setLogLevel(quill::LogLevel level) {
        logger_->set_log_level(level);
    }

    static void shutdown() {
        std::lock_guard<std::mutex> lock(init_mutex_);
        if (is_initialized_) {
            quill::Backend::stop();
            is_initialized_ = false;
        }
    }
};

std::string Logger::filename_;
std::mutex Logger::init_mutex_;
bool Logger::is_initialized_ = false;
