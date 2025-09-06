#include "BinanceExchange.hpp"
#include <iostream>
#include <functional>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <boost/asio/connect.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdexcept> // Include for std::runtime_error

// Aliases for Boost namespaces
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

BinanceExchange::BinanceExchange()
    : ioc_()
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_.get_executor())
    , ws_(ioc_, ctx_) {
    
    // Set up SSL context for client
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(boost::asio::ssl::verify_none);
}

BinanceExchange::~BinanceExchange() noexcept {
    // Destructor
}

void BinanceExchange::initialize(const std::string& host, const std::string& port,
                                 const std::string& target, const boost::json::value& subscription_info) {
    std::cout << "Initializing Binance connection..." << std::endl;
    this->host_ = host;
    this->port_ = port;
    this->target_ = target;
    this->subscription_info_ = subscription_info;
    
    if (subscription_info.is_object() && subscription_info.as_object().contains("streams")) {
        const auto& streams_val = subscription_info.as_object().at("streams");
        if (streams_val.is_array()) {
            // Populate the streams_ vector from the JSON array
            for (const auto& val : streams_val.as_array()) {
                if (val.is_string()) {
                    streams_.push_back(val.as_string().c_str());
                }
            }
        }
    } else {
        throw std::runtime_error("Invalid subscription_info format. 'streams' array is missing.");
    }
    on_generic_ = [](const json::value& data) {
        std::cout << "Received generic data: " << json::serialize(data) << std::endl;
    };


    
    std::cout << "Initialization complete. Ready to start connection." << std::endl;
}

void BinanceExchange::start_async() {
    std::cout << "Starting Binance connection..." << std::endl;
    
    // Build target URL for combined streams
    if (streams_.empty()) {
        std::cerr << "No streams specified!" << std::endl;
        return;
    }
    
    target_ = "/stream?streams=";
    for (size_t i = 0; i < streams_.size(); ++i) {
        if (i > 0) target_ += "/";
        target_ += streams_[i];
    }
    
    std::cout << "Connecting to: " << host_ << ":" << port_ << target_ << std::endl;

    // Start the resolve process
    resolver_.async_resolve(host_, port_,
        std::bind_front(&BinanceExchange::on_resolve, this));
}

void BinanceExchange::run() {
    ioc_.run();
}

void BinanceExchange::stop() {
    std::cout << "Stopping Binance connection..." << std::endl;
    // Post a close operation to the io_context
    net::post(ioc_, [this]() {
        ws_.async_close(websocket::close_code::normal,
            std::bind_front([](beast::error_code ec){
                if (ec) {
                    std::cerr << "Close error: " << ec.message() << std::endl;
                }
                std::cout << "WebSocket connection closed." << std::endl;
            }));
    });
}

void BinanceExchange::send_message(const std::string& message) {
    std::cout << "Sending message: " << message << std::endl;
    net::post(ioc_, [this, message]() {
        ws_.async_write(net::buffer(message),
            std::bind_front([](beast::error_code ec, std::size_t bytes_transferred){
                if (ec) {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                } else {
                    std::cout << "Sent " << bytes_transferred << " bytes." << std::endl;
                }
            }));
    });
}

void BinanceExchange::set_trade_handler(std::function<void(const json::value&)> handler) { on_trade_ = std::move(handler); }
void BinanceExchange::set_agg_trade_handler(std::function<void(const json::value&)> handler) { on_agg_trade_ = std::move(handler); }
void BinanceExchange::set_kline_handler(std::function<void(const json::value&)> handler) { on_kline_ = std::move(handler); }
void BinanceExchange::set_ticker_handler(std::function<void(const json::value&)> handler) { on_ticker_ = std::move(handler); }
void BinanceExchange::set_book_ticker_handler(std::function<void(const json::value&)> handler) { on_book_ticker_ = std::move(handler); }
void BinanceExchange::set_depth_handler(std::function<void(const json::value&)> handler) { on_depth_ = std::move(handler); }
void BinanceExchange::set_generic_handler(std::function<void(const json::value&)> handler) { on_generic_ = std::move(handler); }

void BinanceExchange::add_stream(const std::string& stream) {
    streams_.push_back(stream);
}

void BinanceExchange::subscribe_to_streams(const std::vector<std::string>& new_streams) {
    json::object subscribe_msg;
    subscribe_msg["method"] = "SUBSCRIBE";
    subscribe_msg["params"] = json::array();
    auto& params = subscribe_msg["params"].as_array();
    
    for (const auto& stream : new_streams) {
        params.push_back(json::string(stream));
    }
    subscribe_msg["id"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::string message = json::serialize(subscribe_msg);
    
    net::post(ioc_, [this, message, new_streams]() {
        ws_.async_write(
            net::buffer(message),
            std::bind_front([this, new_streams](beast::error_code ec, std::size_t){
                if (ec) {
                    std::cerr << "Subscribe error: " << ec.message() << std::endl;
                } else {
                    std::cout << "Subscribed to additional streams successfully!" << std::endl;
                    streams_.insert(streams_.end(), new_streams.begin(), new_streams.end());
                }
            }));
    });
}


void BinanceExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        std::cerr << "Resolve error: " << ec.message() << std::endl;
        return;
    }
    
    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
        beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        std::cerr << "SSL SNI error: " << ec.message() << std::endl;
        return;
    }
    
    net::async_connect(
        beast::get_lowest_layer(ws_),
        results,
        std::bind_front(&BinanceExchange::on_connect, this));
}

void BinanceExchange::on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        std::cerr << "Connect error: " << ec.message() << std::endl;
        return;
    }
    
    host_ += ":" + std::to_string(ep.port());

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        std::bind_front(&BinanceExchange::on_ssl_handshake, this));
}

void BinanceExchange::on_ssl_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "SSL handshake error: " << ec.message() << std::endl;
        return;
    }

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(http::field::user_agent, "Binance-Client/1.0");
        }));

    // Perform the websocket handshake
    ws_.async_handshake(host_, target_,
        std::bind_front(&BinanceExchange::on_handshake, this));
}

void BinanceExchange::on_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "Handshake error: " << ec.message() << std::endl;
        return;
    }

    std::cout << "WebSocket connected successfully!" << std::endl;
    
    do_read();
}

void BinanceExchange::do_read() {
    ws_.async_read(
        buffer_,
        std::bind_front(&BinanceExchange::on_read, this));
}

void BinanceExchange::on_read(boost::system::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        std::cerr << "Read error: " << ec.message() << std::endl;
        return;
    }

    // Parse and handle the message
    handle_message();

    // Continue reading
    do_read();
}

void BinanceExchange::handle_message() {
    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        
        json::value parsed = json::parse(message);
        
        route_message(parsed);
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
}

void BinanceExchange::route_message(const json::value& data) {
    try {
        if (data.is_object() && data.as_object().contains("stream") && data.as_object().contains("data")) {
            const auto& stream_name = data.as_object().at("stream").as_string();
            const auto& payload = data.as_object().at("data");

            route_by_event_type(payload);
            return;
        }
        
        route_by_event_type(data);
        
    } catch (const std::exception& e) {
        std::cerr << "Error routing message: " << e.what() << std::endl;
        if (on_generic_) on_generic_(data);
    }
}

void BinanceExchange::route_by_event_type(const json::value& payload) {
    if (!payload.is_object()) {
        if (on_generic_) on_generic_(payload);
        return;
    }

    const auto& obj = payload.as_object();
    
    if (obj.contains("e")) {
        const std::string event_type = obj.at("e").as_string().c_str();
        
        if (event_type == "trade" && on_trade_) {
            on_trade_(payload);
        } else if (event_type == "aggTrade" && on_agg_trade_) {
            on_agg_trade_(payload);
        } else if (event_type == "kline" && on_kline_) {
            on_kline_(payload);
        } else if (event_type == "24hrTicker" && on_ticker_) {
            on_ticker_(payload);
        } else if (event_type == "depthUpdate" && on_depth_) {
            on_depth_(payload);
        } else {
            if (on_generic_) on_generic_(payload);
        }
    } else if (obj.contains("u") && obj.contains("b") && obj.contains("a")) {
        if (on_book_ticker_) on_book_ticker_(payload);
    } else {
        if (on_generic_) on_generic_(payload);
    }
}
