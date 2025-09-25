#pragma once
#include <string>
#include <vector>
#include <functional>
#include <boost/json.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/asio/connect.hpp>

namespace beast = boost::beast;
namespace net = boost::asio;
namespace json = boost::json;


/**
 * @class IExchange
 * @brief An abstract base class defining the public interface for a WebSocket exchange client.
 *
 * This class serves as a template for any  exchange client to implement. It defines the required operations without
 * exposing any internal implementation details.
 */
class IExchange {

    public:
        virtual ~IExchange() = default;

        /**
         * @brief Initializes the exchange connection.
         * @param host The hostname or IP address of the exchange.
         * @param port The port number for the connection.
         * @param target The WebSocket target path.
         * @param subscription_info A vector of streams to subscribe to.
         */
        virtual void initialize(const std::string_view& host, const std::string_view& port,
                                const std::string_view& target, const boost::json::object& subscription_info) = 0;

        /**
         * @brief Starts the asynchronous operation of the exchange client.
         *
         * This method should handle the connection, handshake, and message loop.
         */
        virtual void start() = 0;

        /**
         * @brief Runs the io contect of the exchange client.
         *
         *
         */
        virtual void run() = 0;


        /**
         * @brief Stops the exchange client, closing the connection cleanly.
         */
        virtual void stop() = 0;

        /**
         * @brief Sends a message to the exchange.
         * @param message The message to send as a string.
         */
        virtual void send_message(const std::string& message) = 0;

        /**
         * @brief Read a recieved message from the exchange.
         * @param message The message that is received.
         */

        virtual void read_message() = 0;


        virtual net::io_context& get_io_context() = 0;

};
