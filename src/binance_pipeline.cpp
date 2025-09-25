#include "binance_pipeline.hpp"
#include <iostream>
#include <boost/json.hpp>
#include "utils.hpp"

namespace json = boost::json;


BinancePipeline::BinancePipeline(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus)
    : queue_(queue), exchange_(std::make_shared<BinanceExchange>(queue)), 
      data_parser_(queue, event_bus), event_bus_(event_bus) {}

BinancePipeline::~BinancePipeline() {
    stop();
}

void BinancePipeline::initialize(const std::string& host, const std::string& port,
                                const std::string& target, const boost::json::object& subscription_info) {
    exchange_->initialize(host, port, target, subscription_info);
    name = "binance";

}

void BinancePipeline::start() {
    if (running_) {
        std::cerr << "BinancePipeline already running!" << std::endl;
        return;
    }
    running_ = true;

    // Start the exchange asynchronously
    exchange_->start();

    // Launch exchange thread
    exchange_thread_ = std::thread([this] {
        try {
            std::cout << "Exchange thread started." << std::endl;
            this->exchange_->run();
        } catch (const std::exception& e) {
            std::cerr << "Exchange thread exception: " << e.what() << std::endl;
            running_ = false;
        }
    });

    // Launch parser thread
    parser_thread_ = std::thread([this] {
        try {
            std::cout << "Parser thread started." << std::endl;
            this->data_parser_.start();
        } catch (const std::exception& e) {
            std::cerr << "Parser thread exception: " << e.what() << std::endl;
            running_ = false;
        }
    });

    // Pin the network I/O thread, which is the most latency-sensitive.
    if (exchange_thread_.joinable()) {
        pin_thread_to_cpu(exchange_thread_, 2);
        std::cout << "Pinned exchange thread to CPU 1." << std::endl;
    }

    // Pin the data parsing thread to a different core to run in parallel without contention.
    if (parser_thread_.joinable()) {
        pin_thread_to_cpu(parser_thread_, 3);
        std::cout << "Pinned parser thread to CPU 2." << std::endl;
    }

    std::cout << "BinancePipeline started with market feed and processor threads." << std::endl;
}

void BinancePipeline::stop() {
    if (!running_) {
        return;
    }
    running_ = false;

    // Stop parser first to process remaining queue items
    data_parser_.stop();
    if (parser_thread_.joinable()) {
        parser_thread_.join();
        std::cout << "Parser thread stopped." << std::endl;
    }

    // Stop exchange
    exchange_->stop();
    if (exchange_thread_.joinable()) {
        exchange_thread_.join();
        std::cout << "Exchange thread stopped." << std::endl;
    }

    std::cout << "BinancePipeline stopped." << std::endl;
}