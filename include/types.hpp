#include <string>


struct TradeData {
    std::string symbol;
    double price;
    double quantity;
    int64_t time;
};