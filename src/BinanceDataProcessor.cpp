#include "BinanceDataProcessor.hpp"
#include "BinanceFastParser.hpp"
#include <thread>
#include <iostream>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <charconv>
#include "utils.hpp"

namespace json = boost::json;

BinanceDataProcessor::BinanceDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus)
    : queue_(queue), event_bus_(event_bus) {}

BinanceDataProcessor::~BinanceDataProcessor() {
    stop();
}

void BinanceDataProcessor::start() {
    if (running_) {
        std::cerr << "BinanceDataProcessor already running!" << std::endl;
        return;
    }
    running_ = true;

    std::string message;
    while (running_) {
        if (queue_.try_pop(message)) {
            parse_and_publish(message);
        } else {
            // Prevent busy-waiting with a short sleep
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

void BinanceDataProcessor::stop() {
    running_ = false;
}

void BinanceDataProcessor::parse_and_publish(const std::string& message) {
    const char* start = message.c_str();
    const char* end = start + message.length();

    // 1. Find the start of the "data" object's value (the character '{')
    const char* data_start = BinanceFastParser::find_value_after_key(start, end, "data", 4);
    if (!data_start) return; // "data" key not found

    // 2. Check the event type "e"
    const char* event_type_val = BinanceFastParser::find_value_after_key(data_start, end, "e", 1);

    // We expect "trade", which is 5 characters. strncmp is very fast for this.
    if (strncmp(event_type_val, "trade", 5) == 0) {
        TradeEvent trade_event;
        auto& trade_data = trade_event.data;
        trade_data.source = "Binance";

        // Extract symbol "s"
        const char* symbol_val = BinanceFastParser::find_value_after_key(data_start, end, "s", 1);
        if (symbol_val) {
            const char* symbol_end = static_cast<const char*>(memchr(symbol_val, '"', end - symbol_val));
            if (symbol_end) {
                trade_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
            }
        }

        // Extract and parse price "p"
        const char* price_val = BinanceFastParser::find_value_after_key(data_start, end, "p", 1);
        if (price_val) {
            // The parser will stop at the closing quote
            trade_data.price = BinanceFastParser::parse_double(price_val, end);
        }

        // Extract and parse quantity "q"
        const char* quantity_val = BinanceFastParser::find_value_after_key(data_start, end, "q", 1);
        if (quantity_val) {
            trade_data.quantity = BinanceFastParser::parse_double(quantity_val, end);
        }

        // Extract and parse trade time "T" (this is a raw number, not a string)
        const char* time_val = BinanceFastParser::find_value_after_key(data_start, end, "T", 1);
        if (time_val) {
            // The parser will stop at the next comma or brace
            // trade_data.trade_time = get_time_now();
            // trade_data.trade_time = time_val.;
            trade_data.trade_time = BinanceFastParser::parse_int64(time_val, end) * 10000;

        }
        
        // All data extracted, publish the event
        event_bus_->publish(trade_event);
    }
    else if(strncmp(event_type_val, "depthUpdate", 11) == 0 ){
        OrderBookDataEvent order_book_event;

        order_book_event.data = BinanceFastParser::parse_depth_update(message.c_str(), strlen(message.c_str()));
        event_bus_->publish(order_book_event);


    }
    else if ((strncmp(event_type_val, "ticker", 6) == 0 )){
        
    }

}