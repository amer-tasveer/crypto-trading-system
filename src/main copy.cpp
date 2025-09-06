#include "BinancePipeline.hpp"
// #include "KrakenPipeline.hpp"
#include "SPSCQueue.hpp"
#include "EventBus.hpp"
#include <iostream>
#include <boost/json.hpp>
#include <csignal>
#include <memory>
#include "utils.hpp"
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
        SPSCQueue<std::string> queue(8192); 
        auto event_bus = std::make_shared<EventBus>();


        event_bus->subscribe<TradeEvent>([](const TradeEvent& event) {

        auto givenTimePoint =  event.data.trade_time;
        auto nowTimePoint = get_time_now();
        auto elapsed = nowTimePoint - givenTimePoint;
 
        std::cout << "Received TradeEvent: "
                    << event.data.symbol << ", "
                    << event.data.price << ", "
                    << event.data.quantity << ", "
                    << event.data.trade_time <<", "
                    << elapsed << "\n";

        });

        event_bus->subscribe<OrderBookDataEvent>([](const OrderBookDataEvent& event) {
            auto givenTimePoint =  event.data.event_time;
            auto nowTimePoint = get_time_now();
            auto elapsed = nowTimePoint - givenTimePoint;
            std::cout << "Received OrderBookDataEvent: " << event.data.symbol << ","
            << elapsed << "\n";

            // for (size_t i = 0; i < event.data.bids.size(); ++i) {
            //     std::cout << "(" << event.data.bids[i].first << ", " << event.data.bids[i].second << ")";
            //     if (i < event.data.bids.size() - 1) {
            //         std::cout << ", ";
            //     }
            // }
            // std::cout << "], Ask: [";
            // for (size_t i = 0; i < event.data.asks.size(); ++i) {
            //     std::cout << "(" << event.data.asks[i].first << ", " << event.data.asks[i].second << ")";
            //     if (i < event.data.asks.size() - 1) {
            //         std::cout << ", ";
            //     }
            // }
            // std::cout << "]" << std::endl;
        });

        // Create and configure pipeline
        // BinancePipeline pipeline(queue, event_bus);
        BinancePipeline pipeline(queue, event_bus);

        json::object subscription_info = {
            {"streams", json::array{"btcusdt@trade"}}
            // {"streams", json::array{"btcusdt@depth@100ms"}}

        };

        // json::object subscription_info = {
        //     {"product_ids", json::array{"BTC-USD"}},
        //     {"channels", json::array{"full"}}

        // };

        // json::object subscription_info = {
        //     {"symbol", json::array{"BTC/USD", "MATIC/GBP"}},
        //     {"channel", json::array{"trade"}}

        // };

        // boost::json::object subscription_info;
        // subscription_info["method"] = "subscribe";

        // boost::json::object params;
        // params["channel"] = "book";   // channel type
        // params["symbol"] = json::array{ "BTC/USD" }; // symbols as array

        // subscription_info["params"] = params;


//         g_kraken_exchange->initialize("ws.kraken.com", "443", "", kraken_sub, kraken_raw_queue);

        pipeline.initialize("stream.binance.com", "443", "/ws", subscription_info);

        // pipeline.initialize("ws-feed.exchange.coinbase.com", "443", "/", subscription_info);

        // pipeline.initialize("ws.kraken.com", "443", "/v2", subscription_info);

        // Start pipeline
        pipeline.start();

        // Run until interrupted
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop pipeline
        std::cout << "Shutting down..." << std::endl;
        pipeline.stop();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Main exception: " << e.what() << std::endl;
        return 1;
    }
}