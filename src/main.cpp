#include "binance_pipeline.hpp"
#include "coinbase_pipeline.hpp"
#include "KrakenPipeline.hpp"
#include "SPSCQueue.hpp"
#include "EventBus.hpp"
#include <iostream>
#include <boost/json.hpp>
#include <csignal>
#include <memory>
#include <Logger.hpp>
#include "strats/simple_cross_exchange_arb.hpp"
namespace json = boost::json;

volatile sig_atomic_t g_running = 1;

void signal_handler(int) {
    g_running = 0;
}

int main() {
    try {
        // Set up signal handling for graceful shutdown
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Create queue and event bus
        SPSCQueue<std::string> binance_queue(8192); 
        SPSCQueue<std::string> coinbase_queue(8192); 

        auto event_bus = std::make_shared<EventBus>();

        Logger::init("logs/events.log");

        Logger& logger = Logger::getInstance();
        
        logger.subscribeToBus(event_bus);

        auto execution_router = std::make_shared<IExcecutionRouter>();



        BinancePipeline binance_pipeline(binance_queue, event_bus);
        CoinbasePipeline coinbase_pipeline(coinbase_queue, event_bus);
        // KrakenPipeline pipeline(queue, event_bus);

        json::object binance_subscription_info = {
            // {"streams", json::array{"ethusdt@trade"}}
            {"streams", json::array{"btcusdt@depth@100ms"}}
            // {"streams", json::array{"btcusdt@ticker"}}
            // {"streams", json::array{"btcusdt@kline_1s"}}

        };

        json::object coinbase_subscription_info = {
            {"product_ids", json::array{"BTC-USD"}},
            // {"channels", json::array{"matches"}}
            {"channels", json::array{"level2_batch"}}
            // {"channels", json::array{"ticker"}}

        };

        // json::object subscription_info = {
        //     {"symbol", json::array{"BTC/USD", "MATIC/GBP"}},
        //     {"channel", json::array{"trade"}}

        // };

        boost::json::object subscription_info;
        subscription_info["method"] = "subscribe";

        boost::json::object params;
        params["channel"] = "trade";   // channel type
        params["symbol"] = json::array{ "BTC/USD" }; // symbols as array

        subscription_info["params"] = params;

        binance_pipeline.initialize("stream.binance.com", "443", "/ws", binance_subscription_info);

        coinbase_pipeline.initialize("ws-feed.exchange.coinbase.com", "443", "/", coinbase_subscription_info);

        int16_t diff_percent = 0.00001; 
        CrossExchangeArb arbitrage_strategy(
            event_bus,
            logger, // Pass the logger reference
            execution_router,
            binance_pipeline,
            coinbase_pipeline,
            diff_percent);
            
        arbitrage_strategy.start();

        // // Start pipeline
        // binance_pipeline.start();
        // coinbase_pipeline.start();

        // Run until interrupted
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop pipeline
        std::cout << "Shutting down..." << std::endl;
        binance_pipeline.stop();
        coinbase_pipeline.stop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Main exception: " << e.what() << std::endl;
        return 1;
    }
}
