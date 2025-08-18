#pragma once
#include <string>
#include <vector>
#include <functional>
#include <boost/json.hpp>

namespace json = boost::json;

/**
 * @class IExchange
 * @brief An abstract base class defining the public interface for a WebSocket exchange client.
 *
 * This class serves as a template for any  exchange client to implement. It defines the required operations without
 * exposing any internal implementation details.
 */
class IExchange {
public:
    virtual ~IExchange() = default;

    /**
     * @brief Initializes the exchange connection.
     * @param host The hostname or IP address of the exchange.
     * @param port The port number for the connection.
     * @param target The WebSocket target path.
     * @param streams A vector of streams to subscribe to.
     */
    virtual void initialize(const std::string& host, const std::string& port,
                            const std::string& target, const std::vector<std::string>& streams) = 0;

    /**
     * @brief Starts the asynchronous operation of the exchange client.
     *
     * This method should handle the connection, handshake, and message loop.
     */
    virtual void start_async() = 0;

    /**
     * @brief Runs the io contect of the exchange client.
     *
     *
     */
    virtual void run() = 0;


    /**
     * @brief Stops the exchange client, closing the connection cleanly.
     */
    virtual void stop() = 0;

    /**
     * @brief Sends a message to the exchange.
     * @param message The message to send as a string.
     */
    virtual void send_message(const std::string& message) = 0;

    // The handler setters are already well-designed as non-virtual functions.
    void set_trade_handler(std::function<void(const json::value&)> handler) {
        on_trade_ = std::move(handler);
    }
    
    void set_agg_trade_handler(std::function<void(const json::value&)> handler) {
        on_agg_trade_ = std::move(handler);
    }
    
    void set_kline_handler(std::function<void(const json::value&)> handler) {
        on_kline_ = std::move(handler);
    }
    
    void set_ticker_handler(std::function<void(const json::value&)> handler) {
        on_ticker_ = std::move(handler);
    }
    
    void set_book_ticker_handler(std::function<void(const json::value&)> handler) {
        on_book_ticker_ = std::move(handler);
    }
    
    void set_depth_handler(std::function<void(const json::value&)> handler) {
        on_depth_ = std::move(handler);
    }
    
    void set_generic_handler(std::function<void(const json::value&)> handler) {
        on_generic_ = std::move(handler);
    }

protected:
    std::function<void(const json::value&)> on_trade_;
    std::function<void(const json::value&)> on_agg_trade_;
    std::function<void(const json::value&)> on_kline_;
    std::function<void(const json::value&)> on_ticker_;
    std::function<void(const json::value&)> on_book_ticker_;
    std::function<void(const json::value&)> on_depth_;
    std::function<void(const json::value&)> on_generic_;
};
