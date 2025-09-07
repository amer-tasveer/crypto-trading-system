#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <variant>
#include <new> 

using PriceLevel = std::pair<double, double>;

struct alignas(64) CandleStickData {
    int64_t open_time;
    int64_t close_time;
    int64_t trade_count;
    double open;
    double high;
    double low;
    double close;
    double volume;
    std::string_view source;
    std::string_view symbol;
    std::string_view interval;
};

struct alignas(64) TradeData {
    int64_t trade_time;
    double price;
    double quantity;
    std::string_view source;
    std::string_view symbol;
    std::string_view side;
};

struct alignas(64) TickerData {
    int64_t timestamp;
    double last_price;
    double best_bid;
    double best_bid_size;
    double best_ask;
    double best_ask_size;
    double volume_24h;
    double price_change_24h;
    double price_change_percent_24h;
    double high_24h;
    double low_24h;
    std::string_view source;
    std::string_view symbol;
};

struct alignas(64) OrderBookData {
    int64_t timestamp;
    int64_t id;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    std::string_view source;
    std::string_view symbol;
};

struct Event {};

struct CandleStickDataEvent : Event {
    CandleStickData data;
};

struct TickerDataEvent : Event {
    TickerData data;
};

struct TradeEvent : Event {
    TradeData data;
};

struct OrderBookDataEvent : Event {
    OrderBookData data;
};