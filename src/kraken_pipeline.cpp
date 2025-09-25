#include "kraken_pipeline.hpp"
#include <iostream>
#include <boost/json.hpp>

namespace json = boost::json;

KrakenPipeline::KrakenPipeline(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus)
    : queue_(queue), exchange_(std::make_shared<KrakenExchange>(queue)), 
      data_parser_(queue, event_bus), event_bus_(event_bus) {}

KrakenPipeline::~KrakenPipeline() {
    stop();
}

void KrakenPipeline::initialize(const std::string& host, const std::string& port,
                                const std::string& target, const boost::json::object& subscription_info) {
    exchange_->initialize(host, port, target, subscription_info);
    name = "kraken";

}

void KrakenPipeline::start() {
    if (running_) {
        std::cerr << "KrakenPipeline already running!" << std::endl;
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

    std::cout << "KrakenPipeline started with market feed and processor threads." << std::endl;
}

void KrakenPipeline::stop() {
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

    std::cout << "KrakenPipeline stopped." << std::endl;
}