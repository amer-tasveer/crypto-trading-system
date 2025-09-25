#pragma once
#include "spsc_queue.hpp"
#include "binance_exchange.hpp"
#include "binance_data_processor.hpp"
#include "event_bus.hpp"
#include <thread>
#include <string>

class BinancePipeline {
private:
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<BinanceExchange> exchange_; 
    BinanceDataProcessor data_parser_;
    std::shared_ptr<EventBus> event_bus_;
    std::thread exchange_thread_;
    std::thread parser_thread_;
    bool running_ = false;

public:
    BinancePipeline(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~BinancePipeline();

    void initialize(const std::string& host, const std::string& port, const std::string& target,
                    const boost::json::object& subscription_info);
    void start();
    void stop();
};