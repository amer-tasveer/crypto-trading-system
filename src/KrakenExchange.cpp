#include "KrakenExchange.hpp"
#include <iostream>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/asio/connect.hpp>


namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

KrakenExchange::KrakenExchange(SPSCQueue<std::string>& queue)
    : ioc_(), ctx_(ssl::context::tlsv12_client), resolver_(ioc_.get_executor()),
      ws_(ioc_, ctx_), queue_(queue) {
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_none);
}

KrakenExchange::~KrakenExchange() {
    std::cout << "KrakenExchange destroyed\n";
}

void KrakenExchange::initialize(const std::string& host, const std::string& port,
                                 const std::string& target, const boost::json::object& subscription_info) {
    host_ = host;
    port_ = port;
    target_ = target;
    subscription_info_ = subscription_info;
}

net::io_context& KrakenExchange::get_io_context() {
    return ioc_;
}

void KrakenExchange::start_async() {
    std::cout << "Starting Kraken connection..." << std::endl;
    
    if (subscription_info_.contains("params") && subscription_info_.at("params").is_object()) {
        const auto& params = subscription_info_.at("params").as_object();
        
        // Extract channel
        if (params.contains("channel") && params.at("channel").is_string()) {
            channels_.push_back(params.at("channel").as_string().c_str());
        }
        
        // Extract symbols
        if (params.contains("symbol") && params.at("symbol").is_array()) {
            for (const auto& symbol : params.at("symbol").as_array()) {
                if (symbol.is_string()) {
                    product_ids_.push_back(symbol.as_string().c_str());
                }
            }
            
        }
    } else {
        throw std::runtime_error("Kraken: subscription_info must be an object");
    }
    resolver_.async_resolve(host_, port_,
        std::bind_front(&KrakenExchange::on_resolve, shared_from_this()));
}

void KrakenExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        std::cerr << "Resolve error: " << ec.message() << std::endl;
        return;
    }

    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
        beast::error_code ssl_ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        std::cerr << "SSL SNI error: " << ssl_ec.message() << std::endl;
        return;
    }

    net::async_connect(
        beast::get_lowest_layer(ws_), results,
        std::bind_front(&KrakenExchange::on_connect, this));
}

void KrakenExchange::on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        std::cerr << "Connect error: " << ec.message() << std::endl;
        return;
    }

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        std::bind_front(&KrakenExchange::on_ssl_handshake, this));
}

void KrakenExchange::on_ssl_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "SSL handshake error: " << ec.message() << std::endl;
        return;
    }

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator(
        [this](websocket::request_type& req) {
            req.set(http::field::user_agent, "Kraken-Client/1.0");
        }));

    ws_.async_handshake(host_, target_,
        std::bind_front(&KrakenExchange::on_handshake, this));
}

void KrakenExchange::on_handshake(boost::system::error_code ec) {
    if (ec) { std::cerr << "WS handshake: " << ec.message() << "\n"; return; }
    std::cout << "Connected to Kraken!\n";

    //     // Send single subscription message (like Coinbase)
    // // Send single subscription message (like Coinbase)
    //     if (authenticated_ && needs_authentication()) {
    //         // Get WebSocket token first, then send authenticated subscription
    //         get_websocket_token([this](const std::string& token) {
    //             if (!token.empty()) {
    //                 subscription_info_["params"]["token"] = token;
                    
    //                 std::string msg = json::serialize(subscription_info_);
    //                 send_message(msg);
    //             } else {
    //                 std::cerr << "Failed to get WebSocket authentication token" << std::endl;
    //             }
    //         });
    //     } else {
    //         // Public subscription (no auth needed)
    //         std::string msg = json::serialize(subscription_info_);
    //         send_message(msg);
    //     }
    
    std::string msg = json::serialize(subscription_info_);
    send_message(msg);
    read_message();
}

void KrakenExchange::read_message() {
    ws_.async_read(buffer_, std::bind_front(&KrakenExchange::on_read, shared_from_this()));
}

void KrakenExchange::on_read(boost::system::error_code ec, std::size_t) {
    if (ec) { std::cerr << "Read: " << ec.message() << "\n"; return; }

    std::string msg = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    if (!queue_.try_push(std::move(msg))) {
        std::cerr << "Queue full, dropping message\n";
    }

    read_message();
}

void KrakenExchange::send_message(const std::string& message) {
    ws_.async_write(net::buffer(message),
        [](beast::error_code ec, std::size_t) {
            if (ec) std::cerr << "Write: " << ec.message() << "\n";
        });
}

void KrakenExchange::run() {
    ioc_.run();
}

void KrakenExchange::stop() {
    net::post(ioc_, [self = shared_from_this()](){
        self->ws_.async_close(websocket::close_code::normal,
            [self](beast::error_code ec){
                if (ec) std::cerr << "Close: " << ec.message() << "\n";
                else std::cout << "Closed cleanly\n";
            });
    });
}