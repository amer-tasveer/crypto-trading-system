#include "CoinbaseExchange.hpp"
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

// Aliases for Boost namespaces
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

CoinbaseExchange::CoinbaseExchange()
    : ioc_()
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_.get_executor())
    , ws_(ioc_, ctx_) {
    on_generic_ = [](const json::value& data) {
        // std::cout << "Received generic data: " << json::serialize(data) << std::endl;
    };
    
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(boost::asio::ssl::verify_none);
}

CoinbaseExchange::~CoinbaseExchange() noexcept {
    // Destructor
}

void CoinbaseExchange::initialize(const std::string& host, const std::string& port,
                                 const std::string& target, const std::vector<std::string>& product_ids) {
    std::cout << "Initializing Coinbase connection..." << std::endl;
    this->host_ = host;
    this->port_ = port;
    this->target_ = target;
    this->product_ids_ = product_ids;
    
    std::cout << "Initialization complete. Ready to start connection." << std::endl;
}

void CoinbaseExchange::start_async() {
    std::cout << "Starting Coinbase connection..." << std::endl;
    
    std::cout << "Connecting to: " << host_ << ":" << port_ << target_ << std::endl;

    resolver_.async_resolve(host_, port_,
        std::bind_front(&CoinbaseExchange::on_resolve, this));
}

void CoinbaseExchange::run() {
    ioc_.run();
}

void CoinbaseExchange::stop() {
    std::cout << "Stopping Coinbase connection..." << std::endl;
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

void CoinbaseExchange::send_message(const std::string& message) {
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

void CoinbaseExchange::subscribe(const std::vector<std::string>& product_ids, const std::vector<std::string>& channels) {
    json::object subscribe_msg;
    subscribe_msg["type"] = "subscribe";
    
    json::array product_ids_arr;
    for (const auto& id : product_ids) {
        product_ids_arr.push_back(json::string(id));
    }
    subscribe_msg["product_ids"] = product_ids_arr;
    
    json::array channels_arr;
    for (const auto& channel : channels) {
        channels_arr.push_back(json::string(channel));
    }
    subscribe_msg["channels"] = channels_arr;
    
    std::string message = json::serialize(subscribe_msg);
    send_message(message);
}

void CoinbaseExchange::set_subscription_handler(std::function<void(const json::value&)> handler) { on_subscription_ = std::move(handler); }
void CoinbaseExchange::set_l2update_handler(std::function<void(const json::value&)> handler) { on_l2update_ = std::move(handler); }
void CoinbaseExchange::set_ticker_handler(std::function<void(const json::value&)> handler) { on_ticker_ = std::move(handler); }
void CoinbaseExchange::set_heartbeat_handler(std::function<void(const json::value&)> handler) { on_heartbeat_ = std::move(handler); }
void CoinbaseExchange::set_match_handler(std::function<void(const json::value&)> handler) { on_match_ = std::move(handler); }
void CoinbaseExchange::set_generic_handler(std::function<void(const json::value&)> handler) { on_generic_ = std::move(handler); }


void CoinbaseExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
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
        std::bind_front(&CoinbaseExchange::on_connect, this));
}

void CoinbaseExchange::on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        std::cerr << "Connect error: " << ec.message() << std::endl;
        return;
    }
    
    host_ += ":" + std::to_string(ep.port());

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        std::bind_front(&CoinbaseExchange::on_ssl_handshake, this));
}

void CoinbaseExchange::on_ssl_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "SSL handshake error: " << ec.message() << std::endl;
        return;
    }

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(http::field::user_agent, "Coinbase-Boost-WebSocket-Client");
        }));

    ws_.async_handshake(host_, target_,
        std::bind_front(&CoinbaseExchange::on_handshake, this));
}

void CoinbaseExchange::on_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "Handshake error: " << ec.message() << std::endl;
        return;
    }

    std::cout << "WebSocket connected successfully!" << std::endl;
    
    // Now that we are connected, send the subscription message
    subscribe(product_ids_, { "matches", "heartbeat"});
    
    do_read();
}

void CoinbaseExchange::do_read() {
    ws_.async_read(
        buffer_,
        std::bind_front(&CoinbaseExchange::on_read, this));
}

void CoinbaseExchange::on_read(boost::system::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        std::cerr << "Read error: " << ec.message() << std::endl;
        return;
    }

    handle_message();
    do_read();
}

void CoinbaseExchange::handle_message() {
    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());
        
        json::value parsed = json::parse(message);
        route_message(parsed);
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
}

void CoinbaseExchange::route_message(const json::value& data) {
    if (!data.is_object()) {
        if (on_generic_) on_generic_(data);
        return;
    }

    const auto& obj = data.as_object();
    
    if (obj.contains("type")) {
        const std::string type = obj.at("type").as_string().c_str();
        
        if (type == "subscriptions" && on_subscription_) {
            on_subscription_(data);
        } else if (type == "l2update" && on_l2update_) {
            on_l2update_(data);
        } else if (type == "ticker" && on_ticker_) {
            on_ticker_(data);
        } else if (type == "heartbeat" && on_heartbeat_) {
            on_heartbeat_(data);
        } else if (type == "match" && on_match_) {
            on_match_(data);
        } else {
            if (on_generic_) on_generic_(data);
        }
    } else {
        if (on_generic_) on_generic_(data);
    }
}
