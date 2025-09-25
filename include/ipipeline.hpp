#pragma once

#include "event_bus.hpp"
#include <string>
#include <memory>
#include <boost/json.hpp>


namespace json = boost::json;

class IPipeline {

    public:
        std::shared_ptr<EventBus> event_bus_;
        std::string name;

        virtual void initialize(const std::string& host, const std::string& port, const std::string& target,
                                const boost::json::object& subscription_info) = 0;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual ~IPipeline() = default;

};