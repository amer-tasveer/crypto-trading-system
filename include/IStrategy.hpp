#include <vector>
#include <memory>
#include "EventBus.hpp"
#include "IExchange.hpp"

class IStrategy{

    public:
        std::vector<IExchange> exchanges_;
        std::shared_ptr<EventBus> event_bus_;

        virtual void start() = 0;
        virtual void stop() = 0;

    private:
        std::atomic<bool> running_{false};


};