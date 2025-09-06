#include <string>


struct TradeData {
    std::string symbol;
    double price;
    double quantity;
    int64_t time;
};

struct OrderBookData {
    std::string symbol;
    int64_t time;
    double bidPrice;
    double bidQuantity;
    double askPrice;
    double askQuantity;
};