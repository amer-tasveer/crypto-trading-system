#pragma once
#include "SPSCQueue.hpp"
#include "EventBus.hpp"
#include <string>
#include <memory>

class CoinbaseDataProcessor {
private:
    bool running_ = false;
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<EventBus> event_bus_;

public:
    CoinbaseDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~CoinbaseDataProcessor();

    void start();
    void stop();
    void parse_and_publish(const std::string& message);
};