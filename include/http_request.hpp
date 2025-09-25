#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <string>
#include <iostream>
#include <map>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace json = boost::json;
using tcp = asio::ip::tcp;

class HttpRequest {
public:
    struct Response {
        int status = 0;
        std::string body;
        http::fields headers;
    };

    using Headers = std::map<std::string, std::string>;

    HttpRequest() :
        io_context_(),
        resolver_(io_context_) {}

    Response get(const std::string& host, const std::string& target, const Headers& headers = {}) {
        return send_request(http::verb::get, host, target, headers);
    }

    Response post(const std::string& host, const std::string& target, const json::value& json_body, const Headers& headers = {}) {
        std::string body = json::serialize(json_body);
        Headers post_headers = headers;
        post_headers["Content-Type"] = "application/json";
        return send_request(http::verb::post, host, target, post_headers, body);
    }

private:
    asio::io_context io_context_;
    tcp::resolver resolver_;

    Response send_request(http::verb method, const std::string& host, const std::string& target, const Headers& headers, const std::string& body = "") {
        try {
            auto const results = resolver_.resolve(host, "http");
            tcp::socket stream(io_context_);
            asio::connect(stream, results.begin(), results.end());

            http::request<http::string_body> req{method, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            
            // Add optional headers
            for (const auto& pair : headers) {
                req.set(pair.first, pair.second);
            }

            if (method == http::verb::post && !body.empty()) {
                req.body() = body;
                req.prepare_payload();
            }

            http::write(stream, req);

            beast::flat_buffer b;
            http::response<http::dynamic_body> res;
            http::read(stream, b, res);

            boost::system::error_code ec;
            stream.shutdown(tcp::socket::shutdown_both, ec);

            Response response;
            response.status = static_cast<int>(res.result_int());
            response.headers = res.base();
            response.body = beast::buffers_to_string(res.body().data());

            return response;

        } catch (const boost::system::system_error& se) {
            std::cerr << "Error: " << se.what() << " (" << se.code() << ")" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        return Response();
    }
};