#pragma once
#include <string>
#include <vector>
#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/json.hpp>
#include "IExchange.hpp"
#include "SPSCQueue.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

class CoinbaseExchange : public IExchange, public std::enable_shared_from_this<CoinbaseExchange> {
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
    std::vector<std::string> channels_;

    boost::json::object subscription_info_;
    SPSCQueue<std::string>& queue_;

    // Authentication credentials
    std::string api_key_;
    std::string api_secret_;
    std::string passphrase_;
    bool authenticated_ = false;

    void on_resolve(boost::system::error_code ec, tcp::resolver::results_type results);
    void on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(boost::system::error_code ec);
    void on_handshake(boost::system::error_code ec);
    void on_read(boost::system::error_code ec, std::size_t bytes_transferred);

    // Authentication helper methods
    std::string create_jwt_token(const std::string& request_path) const;
    std::string base64_encode(const std::string& input) const;
    std::string base64_decode(const std::string& input) const;
    std::string hmac_sha256(const std::string& key, const std::string& data) const;
    std::string get_timestamp() const;

public:
    CoinbaseExchange(SPSCQueue<std::string>& queue);
    ~CoinbaseExchange() noexcept override;

    void set_credentials(const std::string& api_key, const std::string& api_secret, 
                        const std::string& passphrase);
    void initialize(const std::string_view& host, const std::string_view& port, const std::string_view& target,
                    const boost::json::object& subscription_info) override;
    net::io_context& get_io_context() override;

    void start_async() override;
    void run() override;
    void stop() override;
    void send_message(const std::string& message) override;
    void read_message() override;
};