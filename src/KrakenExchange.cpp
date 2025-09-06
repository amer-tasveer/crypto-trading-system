#include "KrakenExchange.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <boost/beast/version.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <curl/curl.h>

KrakenExchange::KrakenExchange()
    : ioc_()
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_.get_executor())
    , ws_(ioc_, ctx_) {
    
    on_generic_ = [](const json::value&) {};
    
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(boost::asio::ssl::verify_none);
}

KrakenExchange::~KrakenExchange() noexcept {
    stop();
}

void KrakenExchange::set_credentials(const std::string& api_key, const std::string& api_secret) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    authenticated_ = true;
    std::cout << "KrakenExchange credentials set for authenticated connection." << std::endl;
}

void KrakenExchange::initialize(const std::string& host, const std::string& port,
                                  const std::string& target, const boost::json::value& subscription_info) {
    std::cout << "Initializing Kraken connection..." << std::endl;
    host_ = host;
    port_ = port;
    target_ = target;
    subscription_info_ = subscription_info;
    
    // Map Kraken v2 channels to handlers
    channel_handlers["trade"] = on_trade_;
    channel_handlers["ticker"] = on_ticker_;
    channel_handlers["book"] = on_depth_;

    // Parse subscription info - single subscription format like Coinbase
    if (subscription_info_.is_object()) {
        const auto& obj = subscription_info_.as_object();
        
        if (obj.contains("params") && obj.at("params").is_object()) {
            const auto& params = obj.at("params").as_object();
            
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
        }
    } else {
        throw std::runtime_error("Kraken: subscription_info must be an object");
    }

    std::cout << "Initialization complete. Ready to start connection." << std::endl;
}

void KrakenExchange::start_async() {
    std::cout << "Starting Kraken connection..." << std::endl;
    resolver_.async_resolve(host_, port_,
        std::bind_front(&KrakenExchange::on_resolve, this));
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
    if (ec) {
        std::cerr << "Handshake error: " << ec.message() << std::endl;
        return;
    }

    std::cout << "WebSocket connected successfully!" << std::endl;

    // Send single subscription message (like Coinbase)
    if (!subscription_info_.is_null()) {
        if (authenticated_ && needs_authentication()) {
            // Get WebSocket token first, then send authenticated subscription
            get_websocket_token([this](const std::string& token) {
                if (!token.empty()) {
                    json::object auth_sub = subscription_info_.as_object();
                    auth_sub["params"].as_object()["token"] = token;
                    
                    std::string msg = json::serialize(auth_sub);
                    send_message(msg);
                } else {
                    std::cerr << "Failed to get WebSocket authentication token" << std::endl;
                }
            });
        } else {
            // Public subscription (no auth needed)
            std::string msg = json::serialize(subscription_info_);
            send_message(msg);
        }
    }

    do_read();
}

void KrakenExchange::send_message(const std::string& message) {
    std::cout << "Sending message: " << message << std::endl;
    net::post(ioc_, [this, message]() {
        ws_.async_write(net::buffer(message),
            [this](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                } else {
                    std::cout << "Sent " << bytes_transferred << " bytes." << std::endl;
                }
            });
    });
}

void KrakenExchange::on_read(boost::system::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
        std::cerr << "Read error: " << ec.message() << std::endl;
        return;
    }

    handle_message();
    do_read();
}

void KrakenExchange::do_read() {
    ws_.async_read(buffer_, std::bind_front(&KrakenExchange::on_read, this));
}

void KrakenExchange::run() {
    ioc_.run();
}

void KrakenExchange::handle_message() {
    try {
        std::string message = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        json::value parsed = json::parse(message);
        route_message(parsed);

    } catch (const std::exception& e) {
        std::cerr << "Error handling message: " << e.what() << std::endl;
    }
}

void KrakenExchange::route_message(const json::value& data) {
    if (!data.is_object()) {
        std::cerr << "Route error: Message is not an object" << std::endl;
        return;
    }
    
    const auto& obj = data.as_object();
    std::cout << "Data: " << json::serialize(obj) << std::endl;

    // Handle subscription confirmation messages
    if (obj.contains("method") && obj.at("method").is_string()) {
        std::string method = obj.at("method").as_string().c_str();
        if (method == "subscribe") {
            if (obj.contains("success") && obj.at("success").is_bool() && obj.at("success").as_bool()) {
                std::cout << "Subscription successful" << std::endl;
            } else if (obj.contains("error")) {
                std::cerr << "Subscription error: " << json::serialize(obj.at("error")) << std::endl;
            }
            return;
        }
    }

    // Handle data messages - Kraken v2 format
    if (obj.contains("channel") && obj.at("channel").is_string() &&
        obj.contains("type") && obj.at("type").is_string()) {
        
        std::string channel = obj.at("channel").as_string().c_str();
        std::string type = obj.at("type").as_string().c_str();
        
        if (type == "update") {
            // Look up the channel handler
            auto it = channel_handlers.find(channel);
            if (it != channel_handlers.end() && it->second) {
                it->second(data);
            } else {
                // No specific handler, fall back to generic
                if (on_generic_) {
                    on_generic_(data);
                }
            }
        }
    } else {
        // Fallback to generic handler for unrecognized messages
        if (on_generic_) {
            on_generic_(data);
        }
    }
}

void KrakenExchange::stop() {
    std::cout << "Stopping Kraken connection..." << std::endl;
    net::post(ioc_, [this]() {
        ws_.async_close(websocket::close_code::normal,
            [](beast::error_code ec) {
                if (ec) {
                    std::cerr << "Close error: " << ec.message() << std::endl;
                } else {
                    std::cout << "WebSocket connection closed." << std::endl;
                }
            });
    });
}

// Authentication helper methods
bool KrakenExchange::needs_authentication() const {
    if (!subscription_info_.is_object()) return false;
    
    const auto& obj = subscription_info_.as_object();
    if (!obj.contains("params") || !obj.at("params").is_object()) return false;
    
    const auto& params = obj.at("params").as_object();
    if (!params.contains("channel") || !params.at("channel").is_string()) return false;
    
    std::string channel = params.at("channel").as_string().c_str();
    
    // Channels that require authentication
    return (channel == "executions" || channel == "level3" || channel == "balances");
}

void KrakenExchange::get_websocket_token(std::function<void(const std::string&)> callback) {
    // Prepare POST data and signature
    std::string nonce = std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::string postdata = "nonce=" + nonce;
    
    // Create signature
    std::string path = "/0/private/GetWebSocketsToken";
    std::string signature = create_signature(path, nonce, postdata);
    
    // Prepare headers
    std::map<std::string, std::string> headers = {
        {"API-Key", api_key_},
        {"API-Sign", signature},
        {"User-Agent", "Kraken-Client/1.0"},
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    
    // Make HTTPS request using Boost.Beast
    make_https_request("api.kraken.com", "443", path, postdata, headers,
        [callback](const std::string& response) {
            try {
                json::value parsed = json::parse(response);
                if (parsed.is_object()) {
                    const auto& obj = parsed.as_object();
                    if (obj.contains("result") && obj.at("result").is_object()) {
                        const auto& result = obj.at("result").as_object();
                        if (result.contains("token") && result.at("token").is_string()) {
                            callback(result.at("token").as_string().c_str());
                            return;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error parsing token response: " << e.what() << std::endl;
            }
            
            callback(""); // Failed to get token
        });
}

void KrakenExchange::make_https_request(const std::string& host, const std::string& port,
                                       const std::string& target, const std::string& body,
                                       const std::map<std::string, std::string>& headers,
                                       std::function<void(const std::string&)> callback) {
    
    // Run the HTTPS request in a separate thread to avoid blocking the WebSocket
    std::thread([host, port, target, body, headers, callback]() {
        try {
            // Create a separate io_context for this HTTP request
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            
            // Configure SSL context
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_none); // For simplicity, but consider proper verification in production
            
            // Create resolver and stream
            tcp::resolver resolver{ioc};
            beast::ssl_stream<tcp::socket> stream{ioc, ctx};
            
            // Set SNI hostname
            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                std::cerr << "SSL SNI error: " << ec.message() << std::endl;
                callback("");
                return;
            }
            
            // Resolve the host
            auto const results = resolver.resolve(host, port);
            
            // Connect to the server
            net::connect(beast::get_lowest_layer(stream), results);
            // Perform SSL handshake
            stream.handshake(ssl::stream_base::client);
            
            // Create HTTP POST request
            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::content_type, "application/x-www-form-urlencoded");
            req.set(http::field::content_length, std::to_string(body.length()));
            
            // Add custom headers
            for (const auto& header : headers) {
                req.set(header.first, header.second);
            }
            
            req.body() = body;
            req.prepare_payload();
            
            // Send the request
            http::write(stream, req);
            
            // Read the response
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);
            
            // Shutdown SSL
            beast::error_code ec;
            stream.shutdown(ec);
            // Note: ec might indicate that the connection was closed by the peer,
            // which is normal for HTTP connections
            
            // Return the response body
            callback(res.body());
            
        } catch (const std::exception& e) {
            std::cerr << "HTTPS request error: " << e.what() << std::endl;
            callback("");
        }
    }).detach();
}

std::string KrakenExchange::create_signature(const std::string& path, const std::string& nonce, const std::string& postdata) const {
    // SHA256 hash of (nonce + postdata)
    std::string nonce_postdata = nonce + postdata;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, nonce_postdata.c_str(), nonce_postdata.length());
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);
    EVP_MD_CTX_free(mdctx);
    
    // path + hash
    std::string path_hash = path + std::string(reinterpret_cast<char*>(hash), hash_len);
    
    // HMAC-SHA512 with base64-decoded secret
    std::string decoded_secret = base64_decode(api_secret_);
    
    unsigned char* hmac_result = HMAC(EVP_sha512(),
                                     decoded_secret.c_str(), decoded_secret.length(),
                                     reinterpret_cast<const unsigned char*>(path_hash.c_str()), path_hash.length(),
                                     nullptr, nullptr);
    
    return base64_encode(std::string(reinterpret_cast<char*>(hmac_result), 64));
}

std::string KrakenExchange::base64_encode(const std::string& input) const {
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

std::string KrakenExchange::base64_decode(const std::string& input) const {
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

void KrakenExchange::set_trade_handler(std::function<void(const json::value&)> handler) { 
    on_trade_ = std::move(handler);
    channel_handlers["trade"] = on_trade_;
}

void KrakenExchange::set_agg_trade_handler(std::function<void(const json::value&)> handler) { 
    on_agg_trade_ = std::move(handler);
}

void KrakenExchange::set_kline_handler(std::function<void(const json::value&)> handler) { 
    on_kline_ = std::move(handler);
}

void KrakenExchange::set_ticker_handler(std::function<void(const json::value&)> handler) { 
    on_ticker_ = std::move(handler);
    channel_handlers["ticker"] = on_ticker_;
}

void KrakenExchange::set_book_ticker_handler(std::function<void(const json::value&)> handler) { 
    on_book_ticker_ = std::move(handler);
}

void KrakenExchange::set_depth_handler(std::function<void(const json::value&)> handler) { 
    on_depth_ = std::move(handler);
    channel_handlers["book"] = on_depth_;
}

void KrakenExchange::set_generic_handler(std::function<void(const json::value&)> handler) { 
    on_generic_ = std::move(handler);
}