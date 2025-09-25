#pragma once
#include <vector>
#include <memory>
#include "event_bus.hpp"
#include "iexchange.hpp"
#include "logger.hpp"
#include "iexcecution_router.hpp"

class IStrategy {
public:
    IStrategy(
        std::shared_ptr<EventBus> event_bus,
        Logger& logger,
        std::shared_ptr<IExcecutionRouter> execution_router
    ) : event_bus_{std::move(event_bus)},
        logger_{logger}, 
        execution_router_{std::move(execution_router)}
    {}

    virtual void start() = 0;
    virtual void stop() = 0;
    
protected:
    std::shared_ptr<EventBus> event_bus_;
    Logger& logger_;
    std::shared_ptr<IExcecutionRouter> execution_router_;

private:
    std::atomic<bool> running_{false};
};