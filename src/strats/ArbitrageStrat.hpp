#pragma once
#include <map>
#include <string>
#include <mutex>
#include <string>
#include <iostream>

struct PriceData {
    double price;
    std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;
};

class ArbitrageStrat{

    private:
        std::map<std::string, PriceData> prices_1_;
        std::map<std::string, PriceData> prices_2_;
        std::mutex prices_mutex_;

    public:

        void setPrice(int exchange_id, const std::string& symbol, double price) {
            std::lock_guard<std::mutex> lock(prices_mutex_);
            PriceData data;
            data.price = price;
            data.timestamp = std::chrono::high_resolution_clock::now();

            if (exchange_id == 1) {
                prices_1_[symbol] = data;
                std::cout << "[Exchange 1] " << symbol << " price updated to: " << data.price << std::endl;
            } else if (exchange_id == 2) {
                prices_2_[symbol] = data;
                std::cout << "[Exchange 2] " << symbol << " price updated to: " << data.price << std::endl;
            }
        }
        
        void checkArbitrage(){
            std::lock_guard<std::mutex> lock(prices_mutex_);
            const int MAX_STALE_TIME_MS = 10;
            const double ARBITRAGE_THRESHOLD = 0.001;
            auto now = std::chrono::high_resolution_clock::now();

            for (const auto& pair : prices_1_) {
                const std::string& symbol = pair.first;
                const PriceData& data1 = pair.second;

                auto it2 = prices_2_.find(symbol);
                if (it2 != prices_2_.end()) {
                    const PriceData& data2 = it2->second;

                    auto time_diff1 = std::chrono::duration_cast<std::chrono::milliseconds>(now - data1.timestamp).count();
                    auto time_diff2 = std::chrono::duration_cast<std::chrono::milliseconds>(now - data2.timestamp).count();
                    
                    if (time_diff1 > MAX_STALE_TIME_MS || time_diff2 > MAX_STALE_TIME_MS) {
                        continue;
                    }

                    auto price_time_delta = std::abs(std::chrono::duration_cast<std::chrono::milliseconds>(data1.timestamp - data2.timestamp).count());
                    
                    if (price_time_delta > MAX_STALE_TIME_MS) {
                        continue;
                    }

                    double percentage_diff = std::abs(data1.price - data2.price) / ((data1.price + data2.price) / 2.0);

                    if (percentage_diff > ARBITRAGE_THRESHOLD) {
                        std::cout << "\n ARBITRAGE FOUND! " << std::endl;
                        std::cout << "Symbol: " << symbol << std::endl;
                        std::cout << "Exchange 1 Price: " << data1.price << " (Time Diff: " << time_diff1 << "ms)" << std::endl;
                        std::cout << "Exchange 2 Price: " << data2.price << " (Time Diff: " << time_diff2 << "ms)" << std::endl;
                        std::cout << "Time Delta: " << price_time_delta << "ms" << std::endl;
                        std::cout << "Percentage Difference: " << percentage_diff * 100 << "%" << std::endl;
                        std::cout << "************************************\n" << std::endl;
                    }
                }
            }
        }
};