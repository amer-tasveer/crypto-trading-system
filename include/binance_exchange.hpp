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
#include "iexchange.hpp"
#include "spsc_queue.hpp"

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
namespace json = boost::json;

class BinanceExchange : public IExchange, public std::enable_shared_from_this<BinanceExchange> {
private:
    net::io_context ioc_;
    ssl::context ctx_;
    tcp::resolver resolver_;
    beast::websocket::stream<beast::ssl_stream<tcp::socket>> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::string host_header_;
    std::string port_;
    std::string target_;
    boost::json::object subscription_info_;
    SPSCQueue<std::string>& queue_;

    void on_resolve(boost::system::error_code ec, tcp::resolver::results_type results);
    void on_connect(boost::system::error_code ec, tcp::resolver::results_type::endpoint_type ep);
    void on_ssl_handshake(boost::system::error_code ec);
    void on_handshake(boost::system::error_code ec);
    void on_read(boost::system::error_code ec, std::size_t bytes_transferred);

public:
    BinanceExchange(SPSCQueue<std::string>& queue);
    ~BinanceExchange() noexcept override;
    void initialize(const std::string_view& host, const std::string_view& port, const std::string_view& target,
                    const boost::json::object& subscription_info) override;
    net::io_context& get_io_context() override;
    void start_async() override;
    void run() override;
    void stop() override;
    void send_message(const std::string& message) override;
    void read_message() override;
};