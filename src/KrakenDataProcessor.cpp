#include "KrakenDataProcessor.hpp"
#include "KrakenFastParser.hpp"
#include <thread>
#include <iostream>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <chrono>
#include <charconv>
#include "utils.hpp"

namespace json = boost::json;

KrakenDataProcessor::KrakenDataProcessor(SPSCQueue<std::string>& queue, std::shared_ptr<EventBus> event_bus)
    : queue_(queue), event_bus_(event_bus) {}

KrakenDataProcessor::~KrakenDataProcessor() {
    stop();
}

void KrakenDataProcessor::start() {
    if (running_) {
        std::cerr << "KrakenDataProcessor already running!" << std::endl;
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

void KrakenDataProcessor::stop() {
    running_ = false;
}

void KrakenDataProcessor::parse_and_publish(const std::string& message){
    const char* start = message.c_str();
    const char* end = start + message.length();

    const char* channel_val = KrakenFastParser::find_value_after_key(start, end, "channel", 7);
    const char* type_val = KrakenFastParser::find_value_after_key(start, end, "type", 4);

    if (channel_val && type_val) {
        std::string_view channel_sv(channel_val, std::strcspn(channel_val, "\""));
        std::string_view type_sv(type_val, std::strcspn(type_val, "\""));

        if (channel_sv == "trade") {
            TradeEvent trade_event;
            TradeData& trade_data = trade_event.data;

            trade_data.trade_time = get_time_now_nano();

            const char* data_start = KrakenFastParser::find_value_after_key(start, end, "data", 4);
           
            if (data_start) {
                const char* p = data_start;
                while (p < end && (*p == ' ' || *p == '\t' || *p == '[')) ++p;

                while (p < end && *p != ']') {
                    if (*p == '{') {
                        // Find and parse each field within the trade object
                        const char* symbol_start = KrakenFastParser::find_value_after_key(p, end, "symbol", 6);
                        const char* side_start = KrakenFastParser::find_value_after_key(p, end, "side", 4);
                        const char* price_start = KrakenFastParser::find_value_after_key(p, end, "price", 5);
                        const char* qty_start = KrakenFastParser::find_value_after_key(p, end, "qty", 3);

                        if (symbol_start) {
                            const char* symbol_end = symbol_start;
                            while (symbol_end < end && *symbol_end != '"') ++symbol_end;
                            trade_data.symbol = std::string_view(symbol_start, symbol_end - symbol_start);
                        }
                        if (side_start) {
                            const char* side_end = side_start;
                            while (side_end < end && *side_end != '"') ++side_end;
                            trade_data.side = std::string_view(side_start, side_end - side_start);
                        }
                        if (price_start) {
                            const char* price_end = price_start;
                            while (price_end < end && (*price_end == '.' || (*price_end >= '0' && *price_end <= '9'))) ++price_end;
                            trade_data.price = KrakenFastParser::parse_double(price_start, price_end);
                        }
                        if (qty_start) {
                            const char* qty_end = qty_start;
                            while (qty_end < end && (*qty_end == '.' || (*qty_end >= '0' && *qty_end <= '9'))) ++qty_end;
                            trade_data.quantity = KrakenFastParser::parse_double(qty_start, qty_end);
                        }

                        event_bus_->publish(trade_event);

                        // Find the end of the current object
                        const char* obj_end = qty_start;
                        while(obj_end < end && *obj_end != '}') ++obj_end;
                        if(obj_end < end) ++obj_end; // Skip '}'
                        p = obj_end;

                        // Skip any comma or whitespace between objects
                        while (p < end && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n')) ++p;
                    } else {
                        ++p;
                    }
                }
            }
        } 

        else if (channel_sv == "ohlc") {
            CandleStickDataEvent candlestick_event;
            CandleStickData& candle_data = candlestick_event.data;

            const char* data_start = KrakenFastParser::find_value_after_key(start, end, "data", 4);
            if (data_start) {
                while (data_start < end && *data_start != '[') ++data_start;
                if (data_start < end) ++data_start; // Skip '['
                
                const char* obj_start = static_cast<const char*>(memchr(data_start, '{', end - data_start));
                if (obj_start) {
                    // Find the end of this object
                    const char* obj_end = obj_start;
                    int brace_count = 1;
                    ++obj_end;
                    while (obj_end < end && brace_count > 0) {
                        if (*obj_end == '{') ++brace_count;
                        else if (*obj_end == '}') --brace_count;
                        ++obj_end;
                    }
                    
                    // Parse symbol
                    const char* symbol_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "symbol", 6);
                    if (symbol_val) {
                        const char* symbol_end = symbol_val;
                        while (symbol_end < obj_end && *symbol_end != '"') ++symbol_end;
                        candle_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
                    }

                    // Parse OHLC values
                    const char* open_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "open", 4);
                    if (open_val) {
                        const char* open_end = open_val;
                        while (open_end < obj_end && (*open_end == '.' || *open_end == '-' || (*open_end >= '0' && *open_end <= '9'))) {
                            ++open_end;
                        }
                        candle_data.open = KrakenFastParser::parse_double(open_val, open_end);
                    }

                    const char* high_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "high", 4);
                    if (high_val) {
                        const char* high_end = high_val;
                        while (high_end < obj_end && (*high_end == '.' || *high_end == '-' || (*high_end >= '0' && *high_end <= '9'))) {
                            ++high_end;
                        }
                        candle_data.high = KrakenFastParser::parse_double(high_val, high_end);
                    }

                    const char* low_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "low", 3);
                    if (low_val) {
                        const char* low_end = low_val;
                        while (low_end < obj_end && (*low_end == '.' || *low_end == '-' || (*low_end >= '0' && *low_end <= '9'))) {
                            ++low_end;
                        }
                        candle_data.low = KrakenFastParser::parse_double(low_val, low_end);
                    }

                    const char* close_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "close", 5);
                    if (close_val) {
                        const char* close_end = close_val;
                        while (close_end < obj_end && (*close_end == '.' || *close_end == '-' || (*close_end >= '0' && *close_end <= '9'))) {
                            ++close_end;
                        }
                        candle_data.close = KrakenFastParser::parse_double(close_val, close_end);
                    }

                    const char* volume_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "volume", 6);
                    if (volume_val) {
                        const char* volume_end = volume_val;
                        while (volume_end < obj_end && (*volume_end == '.' || *volume_end == '-' || (*volume_end >= '0' && *volume_end <= '9'))) {
                            ++volume_end;
                        }
                        candle_data.volume = KrakenFastParser::parse_double(volume_val, volume_end);
                    }

                    const char* trades_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "trades", 6);
                    if (trades_val) {
                        const char* trades_end = trades_val;
                        while (trades_end < obj_end && (*trades_end >= '0' && *trades_end <= '9')) {
                            ++trades_end;
                        }
                        candle_data.trade_count = KrakenFastParser::parse_int64(trades_val, trades_end);
                    }

                    const char* interval_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "interval", 8);
                    if (interval_val) {
                        const char* interval_end = interval_val;
                        while (interval_end < obj_end && *interval_end != '"' && *interval_end != ',' && *interval_end != '}') {
                            ++interval_end;
                        }
                        candle_data.interval = std::string_view(interval_val, interval_end - interval_val);
                    }

                    const char* open_time_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "interval_begin", 14);
                    if (open_time_val) {
                        if (*open_time_val == '"') {
                            const char* open_time_end = static_cast<const char*>(memchr(open_time_val + 1, '"', obj_end - (open_time_val + 1)));
                            if (open_time_end) {
                                candle_data.open_time = KrakenFastParser::parse_kraken_timestamp(open_time_val + 1, open_time_end - (open_time_val + 1));
                            }
                        } else {
                            const char* open_time_end = open_time_val;
                            while (open_time_end < obj_end && (*open_time_end >= '0' && *open_time_end <= '9')) {
                                ++open_time_end;
                            }
                            candle_data.open_time = KrakenFastParser::parse_int64(open_time_val, open_time_end);
                        }
                    }

                    const char* close_time_val = KrakenFastParser::find_value_after_key(obj_start, obj_end, "timestamp", 9);
                    if (close_time_val) {
                        if (*close_time_val == '"') {
                            const char* close_time_end = static_cast<const char*>(memchr(close_time_val + 1, '"', obj_end - (close_time_val + 1)));
                            if (close_time_end) {
                                candle_data.close_time = KrakenFastParser::parse_kraken_timestamp(close_time_val + 1, close_time_end - (close_time_val + 1));
                            }
                        } else {
                            const char* close_time_end = close_time_val;
                            while (close_time_end < obj_end && (*close_time_end >= '0' && *close_time_end <= '9')) {
                                ++close_time_end;
                            }
                            candle_data.close_time = KrakenFastParser::parse_int64(close_time_val, close_time_end);
                        }
                    }
                    
                    // Set source field
                    candle_data.source = std::string_view("kraken", 6);
                }
            }
            event_bus_->publish(candlestick_event);
        }

        else if(channel_sv == "ticker") {
            TickerDataEvent ticker_data_event;
            TickerData& ticker_data = ticker_data_event.data;
            const char* data_start = KrakenFastParser::find_value_after_key(start, end, "data", 4);

            if (data_start) {
                const char* ticker_obj_start = static_cast<const char*>(memchr(data_start, '{', end - data_start));
                if (ticker_obj_start) {
                    // Update timestamp
                    ticker_data.timestamp = get_time_now_nano();
                    
                    // Parse symbol
                    const char* symbol_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "symbol", 6);
                    if (symbol_val) {
                        const char* symbol_end = symbol_val;
                        while (symbol_end < end && *symbol_end != '"') ++symbol_end;
                        ticker_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
                    }

                    // Parse numerical fields and map them to your struct
                    const char* last_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "last", 4);
                    if (last_val) {
                        const char* last_end = static_cast<const char*>(memchr(last_val, ',', end - last_val));
                        if (!last_end) last_end = static_cast<const char*>(memchr(last_val, '}', end - last_val));
                        ticker_data.last_price = KrakenFastParser::parse_double(last_val, last_end);
                    }

                    const char* bid_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "bid", 3);
                    if (bid_val) {
                        const char* bid_end = static_cast<const char*>(memchr(bid_val, ',', end - bid_val));
                        if (!bid_end) bid_end = static_cast<const char*>(memchr(bid_val, '}', end - bid_val));
                        ticker_data.best_bid = KrakenFastParser::parse_double(bid_val, bid_end);
                    }
                    
                    const char* bid_qty_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "bid_qty", 7);
                    if (bid_qty_val) {
                        const char* bid_qty_end = static_cast<const char*>(memchr(bid_qty_val, ',', end - bid_qty_val));
                        if (!bid_qty_end) bid_qty_end = static_cast<const char*>(memchr(bid_qty_val, '}', end - bid_qty_val));
                        ticker_data.best_bid_size = KrakenFastParser::parse_double(bid_qty_val, bid_qty_end);
                    }

                    const char* ask_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "ask", 3);
                    if (ask_val) {
                        const char* ask_end = static_cast<const char*>(memchr(ask_val, ',', end - ask_val));
                        if (!ask_end) ask_end = static_cast<const char*>(memchr(ask_val, '}', end - ask_val));
                        ticker_data.best_ask = KrakenFastParser::parse_double(ask_val, ask_end);
                    }
                    
                    const char* ask_qty_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "ask_qty", 7);
                    if (ask_qty_val) {
                        const char* ask_qty_end = static_cast<const char*>(memchr(ask_qty_val, ',', end - ask_qty_val));
                        if (!ask_qty_end) ask_qty_end = static_cast<const char*>(memchr(ask_qty_val, '}', end - ask_qty_val));
                        ticker_data.best_ask_size = KrakenFastParser::parse_double(ask_qty_val, ask_qty_end);
                    }

                    const char* volume_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "volume", 6);
                    if (volume_val) {
                        const char* volume_end = static_cast<const char*>(memchr(volume_val, ',', end - volume_val));
                        if (!volume_end) volume_end = static_cast<const char*>(memchr(volume_val, '}', end - volume_val));
                        ticker_data.volume_24h = KrakenFastParser::parse_double(volume_val, volume_end);
                    }
                    
                    const char* change_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "change", 6);
                    if (change_val) {
                        const char* change_end = static_cast<const char*>(memchr(change_val, ',', end - change_val));
                        if (!change_end) change_end = static_cast<const char*>(memchr(change_val, '}', end - change_val));
                        ticker_data.price_change_24h = KrakenFastParser::parse_double(change_val, change_end);
                    }
                    
                    const char* change_pct_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "change_pct", 10);
                    if (change_pct_val) {
                        const char* change_pct_end = static_cast<const char*>(memchr(change_pct_val, ',', end - change_pct_val));
                        if (!change_pct_end) change_pct_end = static_cast<const char*>(memchr(change_pct_val, '}', end - change_pct_val));
                        ticker_data.price_change_percent_24h = KrakenFastParser::parse_double(change_pct_val, change_pct_end);
                    }
                    
                    const char* high_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "high", 4);
                    if (high_val) {
                        const char* high_end = static_cast<const char*>(memchr(high_val, ',', end - high_val));
                        if (!high_end) high_end = static_cast<const char*>(memchr(high_val, '}', end - high_val));
                        ticker_data.high_24h = KrakenFastParser::parse_double(high_val, high_end);
                    }
                    
                    const char* low_val = KrakenFastParser::find_value_after_key(ticker_obj_start, end, "low", 3);
                    if (low_val) {
                        const char* low_end = static_cast<const char*>(memchr(low_val, ',', end - low_val));
                        if (!low_end) low_end = static_cast<const char*>(memchr(low_val, '}', end - low_val));
                        ticker_data.low_24h = KrakenFastParser::parse_double(low_val, low_end);
                    }
                }
            }
            event_bus_->publish(ticker_data_event);
        }

        else if(channel_sv == "book"){
            OrderBookDataEvent order_book_event;
            OrderBookData& order_book_data = order_book_event.data;
            order_book_data.timestamp = get_time_now_nano();

            const char* data_start = KrakenFastParser::find_value_after_key(start, end, "data", 4);
            if (data_start) {
                
                // Skip to opening bracket of data array
                while (data_start < end && *data_start != '[') ++data_start;
                if (data_start < end) ++data_start; // Skip '['
                
                // Find the order book object within the data array
                const char* book_obj_start = static_cast<const char*>(memchr(data_start, '{', end - data_start));
                if (book_obj_start) {
                    // Find the end of this object
                    const char* book_obj_end = book_obj_start;
                    int brace_count = 1;
                    ++book_obj_end;
                    while (book_obj_end < end && brace_count > 0) {
                        if (*book_obj_end == '{') ++brace_count;
                        else if (*book_obj_end == '}') --brace_count;
                        ++book_obj_end;
                    }
                    
                    // Parse symbol
                    const char* symbol_val = KrakenFastParser::find_value_after_key(book_obj_start, book_obj_end, "symbol", 6);
                    if (symbol_val) {
                        const char* symbol_end = symbol_val;
                        while (symbol_end < book_obj_end && *symbol_end != '"') ++symbol_end;
                        order_book_data.symbol = std::string_view(symbol_val, symbol_end - symbol_val);
                    }

                    // Parse bids
                    const char* bids_start = KrakenFastParser::find_value_after_key(book_obj_start, book_obj_end, "bids", 4);
                    if (bids_start) {
                        // Find the end of bids array
                        const char* bids_end = bids_start;
                        int bracket_count = 0;
                        bool found_start = false;
                        
                        while (bids_end < book_obj_end) {
                            if (*bids_end == '[') {
                                if (!found_start) found_start = true;
                                ++bracket_count;
                            } else if (*bids_end == ']') {
                                --bracket_count;
                                if (found_start && bracket_count == 0) {
                                    ++bids_end; // Include the closing bracket
                                    break;
                                }
                            }
                            ++bids_end;
                        }
                        
                        order_book_data.bids = KrakenFastParser::parse_price_qty_array(bids_start, bids_end);
                    }

                    // Parse asks
                    const char* asks_start = KrakenFastParser::find_value_after_key(book_obj_start, book_obj_end, "asks", 4);
                    if (asks_start) {
                        // Find the end of asks array
                        const char* asks_end = asks_start;
                        int bracket_count = 0;
                        bool found_start = false;
                        
                        while (asks_end < book_obj_end) {
                            if (*asks_end == '[') {
                                if (!found_start) found_start = true;
                                ++bracket_count;
                            } else if (*asks_end == ']') {
                                --bracket_count;
                                if (found_start && bracket_count == 0) {
                                    ++asks_end; // Include the closing bracket
                                    break;
                                }
                            }
                            ++asks_end;
                        }
                        
                        order_book_data.asks = KrakenFastParser::parse_price_qty_array(asks_start, asks_end);
                    }
                }
            }
            event_bus_->publish(order_book_event);
        }
    }
}