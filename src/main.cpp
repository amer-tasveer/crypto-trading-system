#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <csignal>

//Boost stuff
#include <boost/json.hpp>
#include <boost/lexical_cast.hpp>


#include "CoinbaseExchange.hpp"
#include "BinanceExchange.hpp"
#include "types.hpp" 
#include "utils.hpp" 

//Strats
#include "./strats/ArbitrageStrat.hpp"



int main() {
    try {

    ArbitrageStrat arbi_strat;

    std::unique_ptr<CoinbaseExchange> coinbase_exchange;
    std::unique_ptr<BinanceExchange> binance_exchange;

    // Create the exchange client instance.
    coinbase_exchange = std::make_unique<CoinbaseExchange>();
    binance_exchange = std::make_unique<BinanceExchange>();


    coinbase_exchange->set_match_handler([&arbi_strat](const boost::json::value& data) {
        const auto& obj = data.as_object();

        TradeData trade_data = {
            obj.at("product_id").as_string().c_str(),
            boost::lexical_cast<double>(obj.at("price").as_string().c_str()),
            boost::lexical_cast<double>(obj.at("size").as_string().c_str()),
            convert_timestamp_to_milliseconds(obj.at("time").as_string().c_str())
        };

        arbi_strat.setPrice(1,trade_data.symbol, trade_data.price);

        std::cout << "Coinbase TRADE - Symbol: " << trade_data.symbol
                << " | Price: " << trade_data.price
                << " | Quantity: " << trade_data.quantity
                << " | Time: " << convert_milliseconds_to_timestamp(trade_data.time) << std::endl;
    });

    binance_exchange->set_trade_handler([&arbi_strat](const boost::json::value& data) {
        const auto& obj = data.as_object();

        TradeData trade_data = {
            obj.at("s").as_string().c_str(),
            boost::lexical_cast<double>(obj.at("p").as_string().c_str()),
            boost::lexical_cast<double>(obj.at("q").as_string().c_str()),
            obj.at("T").as_int64()
        };
        arbi_strat.setPrice(2,trade_data.symbol, trade_data.price);

        std::cout << "Binance TRADE - Symbol: " << trade_data.symbol
                << " | Price: " << trade_data.price
                << " | Quantity: " << trade_data.quantity
                << " | Time: " << convert_milliseconds_to_timestamp(trade_data.time) << std::endl;
    });

    
    std::vector<std::string> product_ids = {"BTC-USD"};
    
    std::vector<std::string> streams = {
            "btcusdt@trade"   
        };

    binance_exchange->initialize("stream.binance.com", "9443", "", streams);
    binance_exchange->start_async();

    coinbase_exchange->initialize("ws-feed.exchange.coinbase.com", "443", "/", product_ids);
    coinbase_exchange->start_async();

    std::cout << "Starting io_context. Press Ctrl+C to exit." << std::endl;

    std::thread coinbase_thread([&coinbase_exchange]() {
        coinbase_exchange->run();
    });

    std::thread binance_thread([&binance_exchange]() {
        binance_exchange->run();
    });

    while (true) {
        arbi_strat.checkArbitrage();
        // Check for arbitrage every 100ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }


    coinbase_thread.join();
    binance_thread.join();

} catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
}

std::cout << "Program finished." << std::endl;
return 0;
}
