#include "IStrategy.hpp"
#include "IPipeline.hpp"
#include <iomanip> 

struct TradeOpportunity {
    double price_buy;
    double price_sell;
    double volume;
    double expected_profit;
};


class CrossExchangeArb : public IStrategy {
public:
    CrossExchangeArb(
        std::shared_ptr<EventBus> event_bus,
        Logger& logger,
        std::shared_ptr<IExcecutionRouter> execution_router,
        IPipeline& pipeline_1,
        IPipeline& pipeline_2,
        int16_t diff_percent,
        double fee = 0.001 // trading fee as fraction (e.g. 0.1%)
    )
    : IStrategy(event_bus, logger, execution_router),
      pipeline_1_{pipeline_1},
      pipeline_2_{pipeline_2},
      diff_percent_{diff_percent},
      fee_{fee} {}

    void print_orderbook(const OrderBookData& ob) {
        std::cout << "=== OrderBook [" << ob.symbol << "] from " << ob.source << " ===\n";
        std::cout << "Timestamp: " << ob.timestamp << " | ID: " << ob.id << "\n";

        std::cout << "Asks:\n";
        for (const auto& [price, volume] : ob.asks) {
            std::cout << "  Price: " << std::fixed << std::setprecision(8) << price
                    << " | Volume: " << std::fixed << std::setprecision(8) << volume << "\n";
        }

        std::cout << "Bids:\n";
        for (const auto& [price, volume] : ob.bids) {
            std::cout << "  Price: " << std::fixed << std::setprecision(8) << price
                    << " | Volume: " << std::fixed << std::setprecision(8) << volume << "\n";
        }
        // std::cout << "============================================\n";
    }

    void start() override {
        pipeline_1_.start();
        pipeline_2_.start();

        event_bus_->subscribe<OrderBookDataEvent>([this](const OrderBookDataEvent& orderbook_data) { 
            print_orderbook(orderbook_data.data);
            if (orderbook_data.data.source == pipeline_1_.name) {
                orderbook_1_ = orderbook_data.data;
            } else if (orderbook_data.data.source == pipeline_2_.name) {
                orderbook_2_ = orderbook_data.data;
            }

            if (orderbook_1_.has_value() && orderbook_2_.has_value()) {
                auto opp = should_trade(orderbook_1_.value(), orderbook_2_.value());
                if (opp.has_value()) {
                    execute(opp.value());
                }
            }
        });
    }


    void execute(const TradeOpportunity& opp) {
        std::cout << "[ARBITRAGE] BUY @ " << opp.price_buy
                  << " SELL @ " << opp.price_sell
                  << " VOLUME: " << opp.volume
                  << " EXPECTED PROFIT: " << opp.expected_profit << std::endl;
    }
    
    void stop() override {
        pipeline_1_.stop();
        pipeline_2_.stop();
    }

    std::optional<TradeOpportunity> should_trade(const OrderBookData& obA, const OrderBookData& obB) { 
        TradeOpportunity best{0,0,0,0};
        size_t depth = std::min<size_t>(5, std::min(obA.asks.size(), obB.bids.size()));

        for (size_t i = 0; i < depth; ++i) {
            double ask_price = obA.asks[i].first;
            double ask_vol   = obA.asks[i].second;
            for (size_t j = 0; j < depth; ++j) {
                double bid_price = obB.bids[j].first;
                double bid_vol   = obB.bids[j].second;

                double tradable_vol = std::min(ask_vol, bid_vol);
                double spread = bid_price - ask_price;
                double adjusted = spread - fee_ * (ask_price + bid_price) / 2;
                double profit = adjusted * tradable_vol;

                if (profit > best.expected_profit) {
                    best = TradeOpportunity{ask_price, bid_price, tradable_vol, profit};
                }
            }
        }
        if (best.expected_profit > 0) {
            return best;
        }
        return std::nullopt;
    }

private:
    IPipeline& pipeline_1_;
    IPipeline& pipeline_2_;
    std::optional<OrderBookData> orderbook_1_;
    std::optional<OrderBookData> orderbook_2_;
    int16_t diff_percent_;
    double fee_;
};
