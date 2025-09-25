#include "binance_data_processor.hpp"
#include "binance_fast_parser.hpp"
#include <thread>
#include <iostream>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <charconv>
#include "utils.hpp"
#include "simdjson.h"
#include <string>
#include <string_view>


namespace json = boost::json;
static simdjson::ondemand::parser parser;

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


    const char* data_start = BinanceFastParser::find_value_after_key(start, end, "data", 4);
    if (!data_start) return;

    const char* event_type_val = BinanceFastParser::find_value_after_key(data_start, end, "e", 1);
    const char* symbol_val = BinanceFastParser::find_value_after_key(data_start, end, "s", 1);

    if (strncmp(event_type_val, "trade", 5) == 0) {
        TradeEvent trade_event;
        TradeData& trade_data = trade_event.data;
        trade_data.source = "Binance";

        // Extract symbol "s"
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
            trade_data.trade_time = get_time_now_nano();
            // trade_data.trade_time = time_val.;
            // trade_data.trade_time = BinanceFastParser::parse_int64(time_val, end) * 10000;

        }
        
        // All data extracted, publish the event
        event_bus_->publish(trade_event);
    }
    else if(strncmp(event_type_val, "depthUpdate", 11) == 0 ){
        OrderBookDataEvent order_book_event;

        order_book_event.data = BinanceFastParser::parse_depth_update(message.c_str(), strlen(message.c_str()));
        event_bus_->publish(order_book_event);

    }
    else if ((strncmp(event_type_val, "24hrTicker", 6) == 0 )){
        TickerDataEvent tick_event;
        TickerData& tick_data = tick_event.data;
        tick_data.source = "Binance";

        // Extract symbol "s"
        if (symbol_val) {
            const char* symbol_end = static_cast<const char*>(memchr(symbol_val, '"', end - symbol_val));
            if (symbol_end) {
                tick_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
            }
        }

        // Extract and parse price "E"
        const char* timestamp_val = BinanceFastParser::find_value_after_key(data_start, end, "E", 1);
        if (timestamp_val) {
            // tick_data.timestamp = BinanceFastParser::parse_double(timestamp_val, end);
            tick_data.timestamp = get_time_now_nano();
        }

        // Extract symbol "c"
        const char* last_price_val = BinanceFastParser::find_value_after_key(data_start, end, "c", 1);
        if (last_price_val) {
            const char* last_price_end = static_cast<const char*>(memchr(last_price_val, '"', end - last_price_val));
            if (last_price_end) {
                tick_data.last_price = BinanceFastParser::parse_double(last_price_val, end);
            }
        }

        // Extract symbol "b"
        const char* best_bid_val = BinanceFastParser::find_value_after_key(data_start, end, "b", 1);
        if (best_bid_val) {
            const char* best_bid_end = static_cast<const char*>(memchr(best_bid_val, '"', end - best_bid_val));
            if (best_bid_end) {
                tick_data.best_bid = BinanceFastParser::parse_double(best_bid_val, end);
            }
            
        }

        // Extract symbol "B"
        const char* best_bid_size_val = BinanceFastParser::find_value_after_key(data_start, end, "B", 1);
        if (best_bid_size_val) {
            const char* best_bid_size_end = static_cast<const char*>(memchr(best_bid_size_val, '"', end - best_bid_size_val));
            if (best_bid_size_end) {
                tick_data.best_bid_size = BinanceFastParser::parse_double(best_bid_size_val, end);
            }
        }

        // Extract symbol "a"
        const char* best_ask_val = BinanceFastParser::find_value_after_key(data_start, end, "a", 1);
        if (best_ask_val) {
            const char* best_ask_end = static_cast<const char*>(memchr(best_ask_val, '"', end - best_ask_val));
            if (best_ask_end) {
                tick_data.best_ask = BinanceFastParser::parse_double(best_ask_val, end);
            }
            
        }

        // Extract symbol "A"
        const char* best_ask_size_val = BinanceFastParser::find_value_after_key(data_start, end, "A", 1);
        if (best_ask_size_val) {
            const char* best_ask_size_end = static_cast<const char*>(memchr(best_ask_size_val, '"', end - best_ask_size_val));
            if (best_ask_size_end) {
                tick_data.best_bid_size = BinanceFastParser::parse_double(best_ask_size_val, end);
            }
        }

        // Extract symbol "v"
        const char* volume_val = BinanceFastParser::find_value_after_key(data_start, end, "v", 1);
        if (volume_val) {
            const char* volume_end = static_cast<const char*>(memchr(volume_val, '"', end - volume_val));
            if (volume_end) {
                tick_data.volume_24h = BinanceFastParser::parse_double(volume_val, end);
            }
        }

        // Extract symbol "p"
        const char* price_change_val = BinanceFastParser::find_value_after_key(data_start, end, "p", 1);
        if (price_change_val) {
            const char* price_change_end = static_cast<const char*>(memchr(price_change_val, '"', end - price_change_val));
            if (price_change_end) {
                tick_data.price_change_24h = BinanceFastParser::parse_double(price_change_val, end);
            }
        }
        
        // Extract symbol "P"
        const char* price_change_percent_val = BinanceFastParser::find_value_after_key(data_start, end, "P", 1);
        if (price_change_percent_val) {
            const char* price_change_percent_end = static_cast<const char*>(memchr(price_change_percent_val, '"', end - price_change_percent_val));
            if (price_change_percent_end) {
                tick_data.price_change_24h = BinanceFastParser::parse_double(price_change_percent_val, end);
            }
        }

        // Extract symbol "h"
        const char* high_24h_val = BinanceFastParser::find_value_after_key(data_start, end, "h", 1);
        if (high_24h_val) {
            const char* high_24h_end = static_cast<const char*>(memchr(high_24h_val, '"', end - high_24h_val));
            if (high_24h_end) {
                tick_data.high_24h = BinanceFastParser::parse_double(high_24h_val, end);
            }
        }

        // Extract symbol "l"
        const char* low_24h_val = BinanceFastParser::find_value_after_key(data_start, end, "h", 1);
        if (low_24h_val) {
            const char* low_24h_end = static_cast<const char*>(memchr(low_24h_val, '"', end - low_24h_val));
            if (low_24h_end) {
                tick_data.low_24h = BinanceFastParser::parse_double(low_24h_val, end);
            }
        }
        event_bus_->publish(tick_event);

    }
    else if((strncmp(event_type_val, "kline", 5) == 0 )){
        CandleStickDataEvent candlestick_event;
        CandleStickData& candlestick_data = candlestick_event.data;
        candlestick_data.source = "Binance";

        if (symbol_val) {
            const char* symbol_end = static_cast<const char*>(memchr(symbol_val, '"', end - symbol_val));
            if (symbol_end) {
                candlestick_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
            }
        }
        
        // const char* k_start = BinanceFastParser::find_value_after_key(start, end, "k", 4);
       
        // Extract symbol "k.t"
        const char* inteval_val = BinanceFastParser::find_value_after_key(data_start, end, "i", 1);
        if (inteval_val) {
            const char* inteval_end = static_cast<const char*>(memchr(inteval_val, '"', end - inteval_val));
            if (inteval_end) {
                candlestick_data.interval =  std::string_view(inteval_val, inteval_end - inteval_val);
            }
        }

        // Extract symbol "k.t"
        const char* open_time_val = BinanceFastParser::find_value_after_key(data_start, end, "t", 1);
        if (open_time_val) {
            const char* open_time_end = static_cast<const char*>(memchr(open_time_val, '"', end - open_time_val));
            if (open_time_end) {
                candlestick_data.open_time =  BinanceFastParser::parse_int64(open_time_val, open_time_end);
            }
        }

        // Extract symbol "k.o"
        const char* open_val = BinanceFastParser::find_value_after_key(data_start, end, "o", 1);
        if (open_val) {
            const char* open_end = static_cast<const char*>(memchr(open_val, '"', end - open_val));
            if (open_end) {
                candlestick_data.open =  BinanceFastParser::parse_double(open_val, end);
            }
        }

        // Extract symbol "k.h"
        const char* high_val = BinanceFastParser::find_value_after_key(data_start, end, "h", 1);
        if (high_val) {
            const char* high_end = static_cast<const char*>(memchr(high_val, '"', end - high_val));
            if (high_end) {
                candlestick_data.high =  BinanceFastParser::parse_double(high_val, end);
            }
        }

        // Extract symbol "k.l"
        const char* low_val = BinanceFastParser::find_value_after_key(data_start, end, "l", 1);
        if (low_val) {
            const char* low_end = static_cast<const char*>(memchr(low_val, '"', end - low_val));
            if (low_end) {
                candlestick_data.low =  BinanceFastParser::parse_double(low_val, end);
            }
        }

        // Extract symbol "k.c"
        const char* close_val = BinanceFastParser::find_value_after_key(data_start, end, "c", 1);
        if (close_val) {
            const char* close_end = static_cast<const char*>(memchr(close_val, '"', end - close_val));
            if (close_end) {
                candlestick_data.close =  BinanceFastParser::parse_double(close_val, end);
            }
        }

        // Extract symbol "k.v"
        const char* volume_val = BinanceFastParser::find_value_after_key(data_start, end, "v", 1);
        if (volume_val) {
            const char* volume_end = static_cast<const char*>(memchr(volume_val, '"', end - volume_val));
            if (volume_end) {
                candlestick_data.volume =  BinanceFastParser::parse_double(volume_val, end);
            }
        }

        // Extract symbol "k.T"
        const char* close_time_val = BinanceFastParser::find_value_after_key(data_start, end, "T", 1);
        if (close_time_val) {
            const char* close_time_end = static_cast<const char*>(memchr(close_time_val, '"', end - close_time_val));
            if (close_time_end) {
                candlestick_data.close_time =  BinanceFastParser::parse_int64(close_time_val, end);
            }
        }

        // Extract symbol "k.n"
        const char* trade_count_val = BinanceFastParser::find_value_after_key(data_start, end, "n", 1);
        if (trade_count_val) {
            const char* trade_count_end = static_cast<const char*>(memchr(trade_count_val, '"', end - trade_count_val));
            if (trade_count_end) {
                candlestick_data.trade_count =  BinanceFastParser::parse_int64(trade_count_val, end);
            }
        }

        event_bus_->publish(candlestick_event);

    }

}