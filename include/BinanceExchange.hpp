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

class BinanceExchange : public IExchange {
public:
    BinanceExchange();
    ~BinanceExchange() noexcept override;

    void initialize(const std::string& host, const std::string& port,
                    const std::string& target, const std::vector<std::string>& streams) override;
    
    // Asynchronous start, non-blocking.
    void start_async() override;
    // Synchronous run, blocking until stop() is called.
    void run() override;
    
    void stop() override;
    void send_message(const std::string& message) override;

    void set_trade_handler(std::function<void(const json::value&)> handler);
    void set_agg_trade_handler(std::function<void(const json::value&)> handler);
    void set_kline_handler(std::function<void(const json::value&)> handler);
    void set_ticker_handler(std::function<void(const json::value&)> handler);
    void set_book_ticker_handler(std::function<void(const json::value&)> handler);
    void set_depth_handler(std::function<void(const json::value&)> handler);
    void set_generic_handler(std::function<void(const json::value&)> handler);

    void add_stream(const std::string& stream);
    void subscribe_to_streams(const std::vector<std::string>& new_streams);

private:
    net::io_context ioc_;
    ssl::context ctx_;
    tcp::resolver resolver_;
    beast::websocket::stream<beast::ssl_stream<tcp::socket>> ws_;
    beast::flat_buffer buffer_;
    
    // Connection details
    std::string host_;
    std::string port_;
    std::string target_;
    std::vector<std::string> streams_;
    
    // Event handlers
    std::function<void(const json::value&)> on_trade_;
    std::function<void(const json::value&)> on_agg_trade_;
    std::function<void(const json::value&)> on_kline_;
    std::function<void(const json::value&)> on_ticker_;
    std::function<void(const json::value&)> on_book_ticker_;
    std::function<void(const json::value&)> on_depth_;
    std::function<void(const json::value&)> on_generic_;

    // Private asynchronous callbacks
    void on_resolve(boost::system::error_code ec, tcp::resolver::results_type results);
    void on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(boost::system::error_code ec);
    void on_handshake(boost::system::error_code ec);
    void on_read(boost::system::error_code ec, std::size_t bytes_transferred);
    
    // Message parsing and routing logic
    void handle_message();
    void route_message(const json::value& data);
    void route_by_event_type(const json::value& payload);
    
    // Helper function for continuous reading
    void do_read();
};
