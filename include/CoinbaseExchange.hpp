#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/json.hpp>
#include <boost/beast/core/flat_buffer.hpp>

#include "IExchange.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

class CoinbaseExchange : public IExchange {
public:
    CoinbaseExchange();
    ~CoinbaseExchange() noexcept override;

    void initialize(const std::string& host, const std::string& port,
                    const std::string& target, const std::vector<std::string>& product_ids);
    
    // Asynchronous start, non-blocking.
    void start_async() override;
    // Synchronous run, blocking until stop() is called.
    void run() override;
    
    void stop() override;
    void send_message(const std::string& message) override;

    // Specific subscription method for Coinbase
    void subscribe(const std::vector<std::string>& product_ids, const std::vector<std::string>& channels);

    // Handlers for Coinbase message types
    void set_subscription_handler(std::function<void(const json::value&)> handler);
    void set_l2update_handler(std::function<void(const json::value&)> handler);
    void set_ticker_handler(std::function<void(const json::value&)> handler);
    void set_heartbeat_handler(std::function<void(const json::value&)> handler);
    void set_match_handler(std::function<void(const json::value&)> handler);
    void set_generic_handler(std::function<void(const json::value&)> handler);

private:
    net::io_context ioc_;
    ssl::context ctx_;
    tcp::resolver resolver_;
    beast::websocket::stream<beast::ssl_stream<tcp::socket>> ws_;
    beast::flat_buffer buffer_;
    
    std::string host_;
    std::string port_;
    std::string target_;
    std::vector<std::string> product_ids_;
    
    // Handlers for Coinbase events
    std::function<void(const json::value&)> on_subscription_;
    std::function<void(const json::value&)> on_l2update_;
    std::function<void(const json::value&)> on_ticker_;
    std::function<void(const json::value&)> on_heartbeat_;
    std::function<void(const json::value&)> on_match_;
    std::function<void(const json::value&)> on_generic_;

    // Private asynchronous callbacks
    void on_resolve(boost::system::error_code ec, tcp::resolver::results_type results);
    void on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(boost::system::error_code ec);
    void on_handshake(boost::system::error_code ec);
    void on_read(boost::system::error_code ec, std::size_t bytes_transferred);
    
    void handle_message();
    void route_message(const json::value& data);
    
    void do_read();
};
