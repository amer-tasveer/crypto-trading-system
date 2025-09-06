#pragma once
#include "SPSCQueue.hpp"
#include "CoinbaseExchange.hpp"
#include "CoinbaseDataProcessor.hpp"
#include "EventBus.hpp"
#include <thread>
#include <string>

class CoinbasePipeline {
private:
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<CoinbaseExchange> exchange_; 
    CoinbaseDataProcessor data_parser_;
    std::shared_ptr<EventBus> event_bus_;
    std::thread exchange_thread_;
    std::thread parser_thread_;
    bool running_ = false;

public:
    CoinbasePipeline(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~CoinbasePipeline();

    void initialize(const std::string& host, const std::string& port, const std::string& target,
                    const boost::json::object& subscription_info);
    void start();
    void stop();
};