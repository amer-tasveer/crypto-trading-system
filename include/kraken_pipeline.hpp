#pragma once
#include "spsc_queue.hpp"
#include "kraken_exchange.hpp"
#include "kraken_data_processor.hpp"
#include "event_bus.hpp"
#include <thread>
#include <string>

class KrakenPipeline {
private:
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<KrakenExchange> exchange_; 
    KrakenDataProcessor data_parser_;
    std::shared_ptr<EventBus> event_bus_;
    std::thread exchange_thread_;
    std::thread parser_thread_;
    bool running_ = false;

public:
    KrakenPipeline(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~KrakenPipeline();

    void initialize(const std::string& host, const std::string& port, const std::string& target,
                    const boost::json::object& subscription_info);
    void start();
    void stop();
};