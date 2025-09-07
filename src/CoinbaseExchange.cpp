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
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

CoinbaseExchange::CoinbaseExchange(SPSCQueue<std::string>& queue)
    : ioc_()
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_.get_executor())
    , ws_(ioc_, ctx_),queue_(queue) {
    
    
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(boost::asio::ssl::verify_none);
}

CoinbaseExchange::~CoinbaseExchange() noexcept {
    stop();
}

void CoinbaseExchange::set_credentials(const std::string& api_key, const std::string& api_secret, 
                                      const std::string& passphrase) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    passphrase_ = passphrase;
    authenticated_ = true;
    std::cout << "Coinbase credentials set for authenticated connection." << std::endl;
}

void CoinbaseExchange::initialize(const std::string& host, const std::string& port,
                                  const std::string& target, const boost::json::object& subscription_info) {
    std::cout << "Initializing Coinbase connection..." << std::endl;
    host_ = host;
    port_ = port;
    target_ = target;
    subscription_info_ = subscription_info;

    if (subscription_info_.contains("product_ids")) {
        auto& v = subscription_info_["product_ids"];
        if (!v.is_array()) {
            throw std::runtime_error("Coinbase: 'product_ids' must be an array");
        }
        for (const auto& id : v.as_array()) {
            product_ids_.push_back(id.as_string().c_str());
        }
    } else {
        throw std::runtime_error("Coinbase: missing 'product_ids'");
    }

    if (subscription_info_.contains("channels")) {
        auto& v = subscription_info_["channels"];
        if (!v.is_array()) {
            throw std::runtime_error("Coinbase: 'channels' must be an array");
        }
        for (const auto& ch : v.as_array()) {
            channels_.push_back(ch.as_string().c_str());
        }
    } else {
        throw std::runtime_error("Coinbase: missing 'channels'");
    }
    

    std::cout << "Initialization complete. Ready to start connection." << std::endl;
}
net::io_context& CoinbaseExchange::get_io_context() {
    return ioc_;
}
void CoinbaseExchange::start_async() {
    std::cout << "Starting Coinbase connection..." << std::endl;
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
            std::bind_front([](beast::error_code ec) {
                if (ec) {
                    std::cerr << "Close error: " << ec.message() << std::endl;
                } else {
                    std::cout << "WebSocket connection closed." << std::endl;
                }
            }));
    });
}

void CoinbaseExchange::send_message(const std::string& message) {
    std::cout << "Sending message: " << message << std::endl;
    net::post(ioc_, [this, message]() {
        ws_.async_write(net::buffer(message),
            std::bind_front([](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                } else {
                    std::cout << "Sent " << bytes_transferred << " bytes." << std::endl;
                }
            }));
    });
}

void CoinbaseExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
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
        [this](websocket::request_type& req) {
            req.set(http::field::user_agent, "Kraken-Client/1.0");
            
            // Add authentication headers if credentials are provided
            if (authenticated_) {
                std::string timestamp = get_timestamp();
                std::string jwt_token = create_jwt_token("/users/self/verify");
                
                req.set("CB-ACCESS-KEY", api_key_);
                req.set("CB-ACCESS-SIGN", jwt_token);
                req.set("CB-ACCESS-TIMESTAMP", timestamp);
                req.set("CB-ACCESS-PASSPHRASE", passphrase_);
            }
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

    // Add authentication to subscription if needed
    if (authenticated_) {
        
        std::string timestamp = get_timestamp();
        std::string method = "GET";
        std::string request_path = "/users/self/verify";
        
        // Create the prehash string: timestamp + method + request_path + body
        std::string prehash = timestamp + method + request_path;
        
        // Decode the base64 secret and create HMAC
        std::string decoded_secret = base64_decode(api_secret_);
        std::string signature = base64_encode(hmac_sha256(decoded_secret, prehash));
        
        subscription_info_["signature"] = signature;
        subscription_info_["key"] = api_key_;
        subscription_info_["passphrase"] = passphrase_;
        subscription_info_["timestamp"] = timestamp;
        
        std::string msg = json::serialize(subscription_info_);
        send_message(msg);
    } else {
        subscription_info_["type"] = "subscribe";

        std::string msg = json::serialize(subscription_info_);
        send_message(msg);
    }
    

    read_message();
}


void CoinbaseExchange::read_message() {
    ws_.async_read(buffer_, std::bind_front(&CoinbaseExchange::on_read, shared_from_this()));
}

void CoinbaseExchange::on_read(boost::system::error_code ec, std::size_t) {
    if (ec) { std::cerr << "Read: " << ec.message() << "\n"; return; }

    std::string msg = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    if (!queue_.try_push(std::move(msg))) {
        std::cerr << "Queue full, dropping message\n";
    }

    read_message();
}

// Authentication helper methods
std::string CoinbaseExchange::create_jwt_token(const std::string& request_path) const {
    std::string timestamp = get_timestamp();
    std::string message = timestamp + "GET" + request_path;
    return base64_encode(hmac_sha256(base64_decode(api_secret_), message));
}

std::string CoinbaseExchange::base64_encode(const std::string& input) const {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input.c_str(), input.length());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return result;
}

std::string CoinbaseExchange::base64_decode(const std::string& input) const {
    BIO *bio, *b64;
    int decode_len = input.length();
    char* buffer = new char[decode_len];

    bio = BIO_new_mem_buf(input.c_str(), -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    decode_len = BIO_read(bio, buffer, decode_len);
    BIO_free_all(bio);

    std::string result(buffer, decode_len);
    delete[] buffer;

    return result;
}

std::string CoinbaseExchange::hmac_sha256(const std::string& key, const std::string& data) const {
    unsigned char* digest;
    digest = HMAC(EVP_sha256(), 
                  key.c_str(), key.length(),
                  reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
                  nullptr, nullptr);
    
    return std::string(reinterpret_cast<char*>(digest), 32);
}

std::string CoinbaseExchange::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return std::to_string(timestamp);
}