#pragma once
#include "spsc_queue.hpp"
#include "event_bus.hpp"
#include <string>
#include <memory>

class KrakenDataProcessor {
private:
    bool running_ = false;
    SPSCQueue<std::string>& queue_;
    std::shared_ptr<EventBus> event_bus_;

public:
    KrakenDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus);
    ~KrakenDataProcessor();

    void start();
    void stop();
    void parse_and_publish(const std::string& message);
};