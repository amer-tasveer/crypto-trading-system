#pragma once
#include "spsc_queue.hpp"
#include "event_bus.hpp"
#include <string>
#include <memory>

class BinanceDataProcessor {
private:
    bool running_ = false;
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<EventBus> event_bus_;

public:
    BinanceDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~BinanceDataProcessor();

    void start();
    void stop();
    void parse_and_publish(const std::string& message);
};