#include "CoinbaseExchange.hpp"

#include <boost/asio/connect.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/core/buffers_to_string.hpp>

#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace json = boost::json;

using tcp = net::ip::tcp;

//////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
//////////////////////////////////////////////////////////////////////////

CoinbaseExchange::CoinbaseExchange(SPSCQueue<std::string>& queue)
    : ioc_()
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_.get_executor())
    , ws_(ioc_, ctx_)
    , queue_(queue)
{
    ctx_.set_default_verify_paths();
    // For production, DO NOT disable verification. Kept permissive here for convenience during development.
    ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
}

CoinbaseExchange::~CoinbaseExchange() noexcept {
    stop();
}

//////////////////////////////////////////////////////////////////////////
// Public API
//////////////////////////////////////////////////////////////////////////

void CoinbaseExchange::set_credentials(const std::string& api_key, const std::string& api_secret, const std::string& passphrase) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    passphrase_ = passphrase;
    authenticated_ = true;
    std::cout << "[CoinbaseExchange] Credentials set (auth enabled)" << std::endl;
}

void CoinbaseExchange::initialize(const std::string_view& host, const std::string_view& port,
                                  const std::string_view& target, const boost::json::object& subscription_info) {

    host_ = std::string(host);
    port_ = std::string(port);
    target_ = std::string(target);
    subscription_info_ = subscription_info;

    // extract product_ids and channels from subscription_info if present
    if (subscription_info_.contains("product_ids") && subscription_info_["product_ids"].is_array()) {
        for (auto &v : subscription_info_["product_ids"].as_array())
            product_ids_.push_back(v.as_string().c_str());
    }
    if (subscription_info_.contains("channels") && subscription_info_["channels"].is_array()) {
        for (auto &v : subscription_info_["channels"].as_array())
            channels_.push_back(v.as_string().c_str());
    }
}

net::io_context& CoinbaseExchange::get_io_context() {
    return ioc_;
}

void CoinbaseExchange::start() {
    if (running_) return;
    running_ = true;

    // Resolve + connect
    resolver_.async_resolve(host_, port_,
        std::bind_front(&CoinbaseExchange::on_resolve, shared_from_this()));

    // run io_context in separate thread
    run_thread_ = std::thread([this]() {
        try {
            ioc_.run();
        } catch (const std::exception &e) {
            std::cerr << "[CoinbaseExchange] io_context exception: " << e.what() << std::endl;
        }
    });
}

void CoinbaseExchange::run() {
    // deprecated (we run in background thread)
    if (!running_) start();
    if (run_thread_.joinable()) run_thread_.join();
}

void CoinbaseExchange::stop() {
    if (!running_) return;
    running_ = false;

    net::post(ioc_, [this]() {
        beast::error_code ec;
        ws_.next_layer().shutdown(ec);
        ws_.next_layer().next_layer().close(ec);
        ws_.async_close(websocket::close_code::normal, [this](beast::error_code ec) {
            if (ec) std::cerr << "[CoinbaseExchange] Close error: " << ec.message() << std::endl;
        });
    });

    ioc_.stop();
    if (run_thread_.joinable()) run_thread_.join();
}

void CoinbaseExchange::send_message(const std::string& message) {
    net::post(ioc_, [this, message]() {
        ws_.async_write(net::buffer(message), std::bind_front(&CoinbaseExchange::on_write, shared_from_this()));
    });
}

void CoinbaseExchange::read_message() {
    ws_.async_read(buffer_, std::bind_front(&CoinbaseExchange::on_read, shared_from_this()));
}

CoinbaseExchange::OrderBook CoinbaseExchange::snapshot_orderbook() {
    std::lock_guard<std::mutex> lock(orderbook_mutex_);
    return orderbook_;
}

//////////////////////////////////////////////////////////////////////////
// Networking callbacks
//////////////////////////////////////////////////////////////////////////

void CoinbaseExchange::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] Resolve error: " << ec.message() << std::endl;
        return;
    }

    // SNI
    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
        beast::error_code ssl_ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        std::cerr << "[CoinbaseExchange] SSL SNI error: " << ssl_ec.message() << std::endl;
        return;
    }

    net::async_connect(beast::get_lowest_layer(ws_), results,
        std::bind_front(&CoinbaseExchange::on_connect, shared_from_this()));
}

void CoinbaseExchange::on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] Connect error: " << ec.message() << std::endl;
        return;
    }

    // handshake TLS
    ws_.next_layer().async_handshake(ssl::stream_base::client, std::bind_front(&CoinbaseExchange::on_ssl_handshake, shared_from_this()));
}

void CoinbaseExchange::on_ssl_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] SSL handshake error: " << ec.message() << std::endl;
        return;
    }

    // Set common options
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator([this](websocket::request_type& req) {
        req.set(http::field::user_agent, "CoinbaseClient/1.0");
        // If you want to set headers per the authentication docs, it's better to include auth fields in the subscribe payload
    }));

    // handshake websocket
    ws_.async_handshake(host_, target_, std::bind_front(&CoinbaseExchange::on_handshake, shared_from_this()));
}

void CoinbaseExchange::on_handshake(boost::system::error_code ec) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] Handshake error: " << ec.message() << std::endl;
        return;
    }

    std::cout << "[CoinbaseExchange] WebSocket connected." << std::endl;

    // Build subscription JSON
    boost::json::object subscribe_msg;
    subscribe_msg["type"] = "subscribe";

    // product_ids
    boost::json::array pids;
    if (!product_ids_.empty()) {
        for (auto &p : product_ids_) pids.push_back(p);
    } else if (subscription_info_.contains("product_ids") && subscription_info_["product_ids"].is_array()) {
        for (auto &v : subscription_info_["product_ids"].as_array()) pids.push_back(v);
    }
    subscribe_msg["product_ids"] = pids;

    // channels
    boost::json::array chs;
    if (!channels_.empty()) {
        for (auto &c : channels_) chs.push_back(c);
    } else if (subscription_info_.contains("channels") && subscription_info_["channels"].is_array()) {
        for (auto &v : subscription_info_["channels"].as_array()) chs.push_back(v);
    }
    subscribe_msg["channels"] = chs;

    // authentication fields (when required)
    if (authenticated_) {
        std::string timestamp = get_timestamp();
        std::string method = "GET";
        std::string request_path = "/users/self/verify"; // per Coinbase auth example for websocket
        std::string body = ""; // no body for subscribe prehash per example
        std::string signature = create_signature(timestamp, method, request_path, body);

        subscribe_msg["signature"] = signature;
        subscribe_msg["key"] = api_key_;
        subscribe_msg["passphrase"] = passphrase_;
        subscribe_msg["timestamp"] = timestamp;
    }

    std::string msg = json::serialize(subscribe_msg);
    send_message(msg);

    // For L2: fetch REST snapshot(s) for products BEFORE applying WS updates.
    // Start snapshot fetches in background threads if multiple products.
    for (auto &pid : product_ids_) {
        std::thread([this, pid]() { recover_snapshot_for_product(pid); }).detach();
    }

    read_message();
}

void CoinbaseExchange::on_write(boost::system::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] Write error: " << ec.message() << std::endl;
        return;
    }
    (void)bytes_transferred;
}

//////////////////////////////////////////////////////////////////////////
// Message handling
//////////////////////////////////////////////////////////////////////////

void CoinbaseExchange::on_read(boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
    if (ec) {
        std::cerr << "[CoinbaseExchange] Read error: " << ec.message() << std::endl;
        return;
    }

    std::string msg = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());

    // Push raw message into queue (so other consumers see raw feed)
    if (!queue_.try_push(msg)) {
        std::cerr << "[CoinbaseExchange] Queue full, dropping raw message\n";
    }

    // Parse JSON and handle known message types.
    try {
        auto parsed = json::parse(msg);
        if (!parsed.is_object()) {
            read_message();
            return;
        }
        auto obj = parsed.as_object();
        if (!obj.if_contains("type")) {
            read_message();
            return;
        }
        std::string type = obj["type"].as_string().c_str();

        if (type == "snapshot") {
            handle_snapshot_msg(obj);
        } else if (type == "l2update") {
            handle_l2update_msg(obj);
        } else if (type == "open" || type == "done" || type == "change" || type == "match") {
            handle_full_msg(obj);
        } else {
            // other message types: subscriptions, ticker, heartbeat, etc. Ignored here.
        }
    } catch (const std::exception &e) {
        std::cerr << "[CoinbaseExchange] JSON parse error: " << e.what() << " | msg=" << msg << std::endl;
    }

    read_message();
}

//////////////////////////////////////////////////////////////////////////
// Snapshot & update handlers
//////////////////////////////////////////////////////////////////////////

void CoinbaseExchange::handle_snapshot_msg(const json::object& obj) {
    // WebSocket snapshot (rare) - apply similarly to REST snapshot
    if (!obj.if_contains("product_id")) return;
    std::string product = obj.at("product_id").as_string().c_str();

    std::lock_guard<std::mutex> lock(orderbook_mutex_);
    orderbook_.bids.clear();
    orderbook_.asks.clear();
    if (obj.if_contains("bids")) {
        for (auto &lvl : obj.at("bids").as_array()) {
            double price = std::stod(lvl.as_array()[0].as_string().c_str());
            double size = std::stod(lvl.as_array()[1].as_string().c_str());
            orderbook_.bids[price] = size;
        }
    }
    if (obj.if_contains("asks")) {
        for (auto &lvl : obj.at("asks").as_array()) {
            double price = std::stod(lvl.as_array()[0].as_string().c_str());
            double size = std::stod(lvl.as_array()[1].as_string().c_str());
            orderbook_.asks[price] = size;
        }
    }
    if (obj.if_contains("sequence")) orderbook_.last_sequence = obj.at("sequence").as_int64();
    std::cout << "[CoinbaseExchange] Applied WS snapshot for " << product << " seq=" << orderbook_.last_sequence << std::endl;
}

void CoinbaseExchange::handle_l2update_msg(const json::object& obj) {
    // Example l2update:
    // { "type":"l2update", "product_id":"BTC-USD", "changes":[["buy","10101.1","0.45054140"]], "time":"...","sequence": 12345 }
    if (!obj.if_contains("product_id") || !obj.if_contains("sequence")) return;
    std::string product = obj.at("product_id").as_string().c_str();
    int64_t seq = obj.at("sequence").as_int64();

    std::lock_guard<std::mutex> lock(orderbook_mutex_);
    // Ensure we have a base snapshot
    if (orderbook_.last_sequence == 0) {
        // No snapshot loaded yet; fetch now
        lock.unlock();
        recover_snapshot_for_product(product);
        return;
    }

    // Gap detection
    if (seq != orderbook_.last_sequence + 1) {
        std::cerr << "[CoinbaseExchange] Sequence gap detected: last=" << orderbook_.last_sequence << " ws_seq=" << seq << " => recovering snapshot\n";
        lock.unlock();
        recover_snapshot_for_product(product);
        return;
    }

    // Apply changes
    if (obj.if_contains("changes")) {
        for (auto &c : obj.at("changes").as_array()) {
            auto arr = c.as_array();
            std::string side = arr[0].as_string().c_str(); // "buy" or "sell"
            double price = std::stod(arr[1].as_string().c_str());
            double size = std::stod(arr[2].as_string().c_str());

            if (side == "buy") {
                if (size == 0.0) orderbook_.bids.erase(price);
                else orderbook_.bids[price] = size;
            } else {
                if (size == 0.0) orderbook_.asks.erase(price);
                else orderbook_.asks[price] = size;
            }
        }
    }
    orderbook_.last_sequence = seq;
}

void CoinbaseExchange::handle_full_msg(const json::object& obj) {
    // Basic support for 'full' channel types. For production-grade L3,
    // you must track order_ids and per-order sizes.
    if (!obj.if_contains("product_id") || !obj.if_contains("sequence")) return;
    std::string product = obj.at("product_id").as_string().c_str();
    int64_t seq = obj.at("sequence").as_int64();

    std::lock_guard<std::mutex> lock(orderbook_mutex_);
    if (orderbook_.last_sequence == 0) {
        lock.unlock();
        recover_snapshot_for_product(product);
        return;
    }
    if (seq != orderbook_.last_sequence + 1) {
        std::cerr << "[CoinbaseExchange] Full-channel gap detected: last=" << orderbook_.last_sequence << " ws_seq=" << seq << " => recovering\n";
        lock.unlock();
        recover_snapshot_for_product(product);
        return;
    }

    std::string type = obj.at("type").as_string().c_str();
    std::string side = obj.if_contains("side") ? obj.at("side").as_string().c_str() : "";
    double price = 0.0;
    double size = 0.0;
    if (obj.if_contains("price")) price = std::stod(obj.at("price").as_string().c_str());
    if (obj.if_contains("remaining_size")) size = std::stod(obj.at("remaining_size").as_string().c_str());
    if (obj.if_contains("size")) size = std::stod(obj.at("size").as_string().c_str());

    if (type == "open") {
        if (side == "buy") orderbook_.bids[price] = size;
        else orderbook_.asks[price] = size;
    } else if (type == "change") {
        if (side == "buy") orderbook_.bids[price] = size;
        else orderbook_.asks[price] = size;
    } else if (type == "done") {
        // done may contain reason; remove level
        if (side == "buy") orderbook_.bids.erase(price);
        else orderbook_.asks.erase(price);
    } else if (type == "match") {
        // reduce size by match size
        double match_size = obj.if_contains("size") ? std::stod(obj.at("size").as_string().c_str()) : 0.0;
        if (side == "buy") {
            auto it = orderbook_.bids.find(price);
            if (it != orderbook_.bids.end()) {
                it->second -= match_size;
                if (it->second <= 0.0) orderbook_.bids.erase(it);
            }
        } else {
            auto it = orderbook_.asks.find(price);
            if (it != orderbook_.asks.end()) {
                it->second -= match_size;
                if (it->second <= 0.0) orderbook_.asks.erase(it);
            }
        }
    }

    orderbook_.last_sequence = seq;
}

//////////////////////////////////////////////////////////////////////////
// Snapshot recovery (REST)
//////////////////////////////////////////////////////////////////////////

void CoinbaseExchange::recover_snapshot_for_product(const std::string& product_id) {
    // fetch REST snapshot level=2 (per docs)
    if (fetch_level2_snapshot(product_id)) {
        std::cout << "[CoinbaseExchange] Snapshot recovered for " << product_id << " seq=" << orderbook_.last_sequence << std::endl;
    } else {
        std::cerr << "[CoinbaseExchange] Snapshot fetch FAILED for " << product_id << std::endl;
    }
}

bool CoinbaseExchange::fetch_level2_snapshot(const std::string& product_id) {
    // Use HttpRequest helper
    HttpRequest http;
    std::string host = "api.exchange.coinbase.com";
    std::string target = "/products/" + product_id + "/book?level=2";

    auto resp = http.get(host, target, { {"Accept", "application/json"} });
    if (resp.status != 200) {
        std::cerr << "[CoinbaseExchange] REST snapshot status=" << resp.status << " body=" << resp.body << std::endl;
        return false;
    }

    try {
        auto parsed = json::parse(resp.body).as_object();

        std::lock_guard<std::mutex> lock(orderbook_mutex_);
        orderbook_.bids.clear();
        orderbook_.asks.clear();

        if (parsed.if_contains("bids")) {
            for (auto &lvl : parsed.at("bids").as_array()) {
                double price = std::stod(lvl.as_array()[0].as_string().c_str());
                double size = std::stod(lvl.as_array()[1].as_string().c_str());
                orderbook_.bids[price] = size;
            }
        }
        if (parsed.if_contains("asks")) {
            for (auto &lvl : parsed.at("asks").as_array()) {
                double price = std::stod(lvl.as_array()[0].as_string().c_str());
                double size = std::stod(lvl.as_array()[1].as_string().c_str());
                orderbook_.asks[price] = size;
            }
        }
        if (parsed.if_contains("sequence")) orderbook_.last_sequence = parsed.at("sequence").as_int64();
        else orderbook_.last_sequence = 0;

        return true;
    } catch (const std::exception &e) {
        std::cerr << "[CoinbaseExchange] Snapshot parse error: " << e.what() << std::endl;
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Crypto helper functions
//////////////////////////////////////////////////////////////////////////

// Create signature base64(HMAC_SHA256(base64-decoded(secret), prehash))
std::string CoinbaseExchange::create_signature(const std::string& timestamp, const std::string& method,
                                               const std::string& request_path, const std::string& body) const {
    std::string prehash = timestamp + method + request_path + body;
    std::string decoded_secret = base64_decode(api_secret_);
    std::string raw_hmac = hmac_sha256_raw(decoded_secret, prehash);
    return base64_encode(raw_hmac);
}

// HMAC-SHA256 raw binary output as std::string
std::string CoinbaseExchange::hmac_sha256_raw(const std::string& key, const std::string& data) const {
    unsigned int len = EVP_MAX_MD_SIZE;
    unsigned char out[EVP_MAX_MD_SIZE];
    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size()), EVP_sha256(), nullptr);
    HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    HMAC_Final(ctx, out, &len);
    HMAC_CTX_free(ctx);
    return std::string(reinterpret_cast<char*>(out), static_cast<size_t>(len));
}

// Base64 encode binary/string
std::string CoinbaseExchange::base64_encode(const std::string& input) const {
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, input.data(), static_cast<int>(input.size()));
    BIO_flush(b64);
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string out(bptr->data, bptr->length);
    BIO_free_all(b64);
    return out;
}

// Base64 decode (returns binary string)
std::string CoinbaseExchange::base64_decode(const std::string& input) const {
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new_mem_buf(const_cast<char*>(input.data()), static_cast<int>(input.size()));
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64, bmem);

    std::string out;
    out.resize(input.size());
    int decoded_len = BIO_read(bmem, &out[0], static_cast<int>(input.size()));
    BIO_free_all(bmem);
    if (decoded_len <= 0) return {};
    out.resize(decoded_len);
    return out;
}

std::string CoinbaseExchange::get_timestamp() const {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ts = duration_cast<seconds>(now.time_since_epoch()).count();
    return std::to_string(ts);
}
