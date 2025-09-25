#include "coinbase_data_processor.hpp"
#include "coinbase_fast_parser.hpp"
#include <thread>
#include <iostream>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <charconv>
#include "utils.hpp"
#include "simdjson.h"

namespace json = boost::json;
static simdjson::ondemand::parser parser;

CoinbaseDataProcessor::CoinbaseDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus)
    : queue_(queue), event_bus_(event_bus) {}

CoinbaseDataProcessor::~CoinbaseDataProcessor() {
    stop();
}

void CoinbaseDataProcessor::start() {
    if (running_) {
        std::cerr << "CoinbaseDataProcessor already running!" << std::endl;
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

void CoinbaseDataProcessor::stop() {
    running_ = false;
}

void CoinbaseDataProcessor::parse_and_publish(const std::string& message) {
    const char* start = message.c_str();
    const char* end = start + message.length();
    
    const char* event_type_val = CoinbaseFastParser::find_value_after_key(start, end, "type", 4);
    const char* symbol_val = CoinbaseFastParser::find_value_after_key(start, end, "product_id", 10);

    if (strncmp(event_type_val, "match", 5) == 0) {
        TradeEvent trade_event;
        TradeData& trade_data = trade_event.data;
        trade_data.source = "Coinbase";

        // Extract symbol "product_id"
        if (symbol_val) {
            const char* symbol_end = static_cast<const char*>(memchr(symbol_val, '"', end - symbol_val));
            if (symbol_end) {
                trade_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
            }
        }

        // Extract and parse price "price"
        const char* price_val = CoinbaseFastParser::find_value_after_key(start, end, "price", 5);
        if (price_val) {
            // The parser will stop at the closing quote
            trade_data.price = CoinbaseFastParser::parse_double(price_val, end);
        }

        // Extract and parse quantity "size"
        const char* quantity_val = CoinbaseFastParser::find_value_after_key(start, end, "size", 4);
        if (quantity_val) {
            trade_data.quantity = CoinbaseFastParser::parse_double(quantity_val, end);
        }

        // Extract and parse trade time "time"
        const char* time_val = CoinbaseFastParser::find_value_after_key(start, end, "time", 4);
        if (time_val) {

            trade_data.trade_time = get_time_now_nano();
            // trade_data.trade_time = time_val;

        }
        
        event_bus_->publish(trade_event);
    }
    else if (strncmp(event_type_val, "ticker", 6) == 0) {
        TickerDataEvent ticker_event;
        TickerData& tick_data = ticker_event.data;
        tick_data.source = "Coinbase";

        // Extract symbol "product_id"
        if (symbol_val) {
            const char* symbol_end = static_cast<const char*>(memchr(symbol_val, '"', end - symbol_val));
            if (symbol_end) {
                tick_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
            }
        }

        // Extract and parse trade time "time"
        const char* time_val = CoinbaseFastParser::find_value_after_key(start, end, "time", 4);
        if (time_val) {

            tick_data.timestamp = get_time_now_nano();
            // trade_data.trade_time = time_val;

        }


        // Extract symbol "price"
        const char* last_price_val = CoinbaseFastParser::find_value_after_key(start, end, "price", 5);
        if (last_price_val) {
            const char* last_price_end = static_cast<const char*>(memchr(last_price_val, '"', end - last_price_val));
            if (last_price_end) {
                tick_data.last_price = CoinbaseFastParser::parse_double(last_price_val, end);
            }
        }

        // Extract symbol "best_bid"
        const char* best_bid_val = CoinbaseFastParser::find_value_after_key(start, end, "best_bid", 8);
        if (best_bid_val) {
            const char* best_bid_end = static_cast<const char*>(memchr(best_bid_val, '"', end - best_bid_val));
            if (best_bid_end) {
                tick_data.best_bid = CoinbaseFastParser::parse_double(best_bid_val, end);
            }
            
        }

        // Extract symbol "best_bid_size"
        const char* best_bid_size_val = CoinbaseFastParser::find_value_after_key(start, end, "best_bid_size", 13);
        if (best_bid_size_val) {
            const char* best_bid_size_end = static_cast<const char*>(memchr(best_bid_size_val, '"', end - best_bid_size_val));
            if (best_bid_size_end) {
                tick_data.best_bid_size = CoinbaseFastParser::parse_double(best_bid_size_val, end);
            }
        }

        // Extract symbol "best_ask"
        const char* best_ask_val = CoinbaseFastParser::find_value_after_key(start, end, "best_ask", 8);
        if (best_ask_val) {
            const char* best_ask_end = static_cast<const char*>(memchr(best_ask_val, '"', end - best_ask_val));
            if (best_ask_end) {
                tick_data.best_ask = CoinbaseFastParser::parse_double(best_ask_val, end);
            }
            
        }

        // Extract symbol "best_ask_size"
        const char* best_ask_size_val = CoinbaseFastParser::find_value_after_key(start, end, "best_ask_size", 13);
        if (best_ask_size_val) {
            const char* best_ask_size_end = static_cast<const char*>(memchr(best_ask_size_val, '"', end - best_ask_size_val));
            if (best_ask_size_end) {
                tick_data.best_bid_size = CoinbaseFastParser::parse_double(best_ask_size_val, end);
            }
        }

        // Extract symbol "volume_24h"
        const char* volume_val = CoinbaseFastParser::find_value_after_key(start, end, "volume_24h", 10);
        if (volume_val) {
            const char* volume_end = static_cast<const char*>(memchr(volume_val, '"', end - volume_val));
            if (volume_end) {
                tick_data.volume_24h = CoinbaseFastParser::parse_double(volume_val, end);
            }
        }

        // Extract symbol "price_24h"
        const char* price_change_val = CoinbaseFastParser::find_value_after_key(start, end, "price_24h", 9);
        if (price_change_val) {
            const char* price_change_end = static_cast<const char*>(memchr(price_change_val, '"', end - price_change_val));
            if (price_change_end) {
                tick_data.price_change_24h = CoinbaseFastParser::parse_double(price_change_val, end);
            }
        }


        const char* open_24h_val = CoinbaseFastParser::find_value_after_key(start, end, "open_24h", 8);
        if (open_24h_val) {
            const char* open_24h_end = static_cast<const char*>(memchr(open_24h_val, '"', end - open_24h_val));
            if (open_24h_end) {
                double parsed = CoinbaseFastParser::parse_double(open_24h_val, end);
                tick_data.price_change_percent_24h = (tick_data.last_price - parsed) / parsed;
            }
        }


        // Extract symbol "high_24h"
        const char* high_24h_val = CoinbaseFastParser::find_value_after_key(start, end, "high_24h", 8);
        if (high_24h_val) {
            const char* high_24h_end = static_cast<const char*>(memchr(high_24h_val, '"', end - high_24h_val));
            if (high_24h_end) {
                tick_data.high_24h = CoinbaseFastParser::parse_double(high_24h_val, end);
            }
        }

        // Extract symbol "low_24h"
        const char* low_24h_val = CoinbaseFastParser::find_value_after_key(start, end, "low_24h", 7);
        if (low_24h_val) {
            const char* low_24h_end = static_cast<const char*>(memchr(low_24h_val, '"', end - low_24h_val));
            if (low_24h_end) {
                tick_data.low_24h = CoinbaseFastParser::parse_double(low_24h_val, end);
            }
        }
        
        event_bus_->publish(ticker_event);
    }
    else if ((strncmp(event_type_val, "l2update", 8) == 0) ){
        OrderBookDataEvent order_book_event;
        order_book_event.data = CoinbaseFastParser::parse_depth_update(message.c_str(), strlen(message.c_str()));
        event_bus_->publish(order_book_event);
    }
}

// void CoinbaseDataProcessor::parse_and_publish(const std::string& message)  {
//     std::cout << message << "\n";

//     // Create a padded string for simdjson parsing
//     simdjson::padded_string json_string(message);
//     simdjson::ondemand::document doc;
//     simdjson::error_code error;

//     // Use the parser to parse the document
//     error = parser.iterate(json_string).get(doc);
//     if (error) {
//         std::cerr << "simdjson parsing error: " << simdjson::error_message(error) << std::endl;
//         return;
//     }

//     // Access the "data" object
//     simdjson::ondemand::object data_obj;
//     error = doc["data"].get(data_obj);
//     if (error) {
//         return; // "data" key not found
//     }

//     // Check the event type "e"
//     std::string_view event_type_val;
//     error = data_obj["e"].get_string().get(event_type_val);
//     if (error) {
//         return; // "e" key not found
//     }

//     // Use string_view comparison for efficiency
//     if (event_type_val == "trade") {
//         TradeEvent trade_event;
//         auto& trade_data = trade_event.data;
//         trade_data.source = "Coinbase";

//         // Extract symbol "s"
//         error = data_obj["s"].get_string().get(trade_data.symbol);
//         if (error) {
//             std::cerr << "Error getting symbol: " << simdjson::error_message(error) << std::endl;
//             return;
//         }

//         // Extract and parse price "p"
//         double price_val;
//         error = data_obj["p"].get_string().get_double().get(price_val);
//         if (error) {
//             std::cerr << "Error getting price: " << simdjson::error_message(error) << std::endl;
//             return;
//         }
//         trade_data.price = price_val;

//         // Extract and parse quantity "q"
//         double quantity_val;
//         error = data_obj["q"].get_string().get_double().get(quantity_val);
//         if (error) {
//             std::cerr << "Error getting quantity: " << simdjson::error_message(error) << std::endl;
//             return;
//         }
//         trade_data.quantity = quantity_val;

//         // Extract and parse trade time "T"
//         // Note: Coinbase's 'T' is a timestamp in milliseconds, not a string
//         int64_t time_val;
//         error = data_obj["T"].get_int64().get(time_val);
//         if (error) {
//             std::cerr << "Error getting trade time: " << simdjson::error_message(error) << std::endl;
//             return;
//         }
//         trade_data.trade_time = time_val; // Use the provided timestamp

//         // All data extracted, publish the event
//         event_bus_->publish(trade_event);
//     } else if (event_type_val == "depthUpdate") {
//         OrderBookDataEvent order_book_event;
//         // The implementation for depthUpdate would be a separate function
//         // order_book_event.data = simdjson_parse_depth_update(message);
//         event_bus_->publish(order_book_event);
//     }
// }