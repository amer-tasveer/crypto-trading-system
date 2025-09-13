#include "BinanceExchange.hpp"
#include <iostream>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/asio/connect.hpp>


namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

BinanceExchange::BinanceExchange(SPSCQueue<std::string>& queue)
    : ioc_(), ctx_(ssl::context::tlsv12_client), resolver_(ioc_.get_executor()),
      ws_(ioc_, ctx_), queue_(queue) {
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_none);
}

BinanceExchange::~BinanceExchange() {
    std::cout << "BinanceExchange destroyed\n";
}

void BinanceExchange::initialize(const std::string_view& host, const std::string_view& port,
                                 const std::string_view& target, const boost::json::object& subscription_info) {
    host_ = host;
    port_ = port;
    target_ = target;
    subscription_info_ = subscription_info;
}

net::io_context& BinanceExchange::get_io_context() {
    return ioc_;
}

void BinanceExchange::start_async() {
    std::cout << "Starting Binance connection..." << std::endl;

    std::vector<std::string> streams;
    if (subscription_info_.contains("streams")) {
        const auto& streams_val = subscription_info_.at("streams");
        if (streams_val.is_array()) {
            for (const auto& val : streams_val.as_array()) {
                if (val.is_string()) streams.push_back(std::string(val.as_string().c_str()));
            }
        }
    } else {
        throw std::runtime_error("Invalid subscription_info format. 'streams' array is missing.");
    }

    if (streams.empty()) {
        std::cerr << "No streams specified!" << std::endl;
        return;
    }

    target_ = "/stream?streams=";
    for (size_t i = 0; i < streams.size(); ++i) {
        if (i > 0) target_ += "/";
        target_ += streams[i];
    }

    std::cout << "Connecting to: " << host_ << ":" << port_ << target_ << std::endl;

    resolver_.async_resolve(host_, port_,
        std::bind_front(&BinanceExchange::on_resolve, shared_from_this()));
}

void BinanceExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
    if (ec) { std::cerr << "Resolve: " << ec.message() << "\n"; return; }
    net::async_connect(beast::get_lowest_layer(ws_), results,
        std::bind_front(&BinanceExchange::on_connect, shared_from_this()));
}

void BinanceExchange::on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) { std::cerr << "Connect: " << ec.message() << "\n"; return; }

    host_header_ = host_;
    if (port_ != "443") host_header_ += ":" + std::to_string(ep.port());

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req){
        req.set(beast::http::field::user_agent, "Binance-Client/1.0");
    }));

    ws_.next_layer().async_handshake(ssl::stream_base::client,
        std::bind_front(&BinanceExchange::on_ssl_handshake, shared_from_this()));
}

void BinanceExchange::on_ssl_handshake(boost::system::error_code ec) {
    if (ec) { std::cerr << "TLS handshake: " << ec.message() << "\n"; return; }
    ws_.async_handshake(host_header_, target_.empty() ? "/ws" : target_,
        std::bind_front(&BinanceExchange::on_handshake, shared_from_this()));
}

void BinanceExchange::on_handshake(boost::system::error_code ec) {
    if (ec) { std::cerr << "WS handshake: " << ec.message() << "\n"; return; }
    std::cout << "Connected to Binance!\n";
    read_message();
}

void BinanceExchange::read_message() {
    ws_.async_read(buffer_, std::bind_front(&BinanceExchange::on_read, shared_from_this()));
}

void BinanceExchange::on_read(boost::system::error_code ec, std::size_t) {
    if (ec) { std::cerr << "Read: " << ec.message() << "\n"; return; }

    std::string msg = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    if (!queue_.try_push(std::move(msg))) {
        std::cerr << "Queue full, dropping message\n";
    }

    read_message();
}

void BinanceExchange::send_message(const std::string& message) {
    ws_.async_write(net::buffer(message),
        [](beast::error_code ec, std::size_t) {
            if (ec) std::cerr << "Write: " << ec.message() << "\n";
        });
}

void BinanceExchange::run() {
    ioc_.run();
}

void BinanceExchange::stop() {
    net::post(ioc_, [self = shared_from_this()](){
        self->ws_.async_close(websocket::close_code::normal,
            [self](beast::error_code ec){
                if (ec) std::cerr << "Close: " << ec.message() << "\n";
                else std::cout << "Closed cleanly\n";
            });
    });
}