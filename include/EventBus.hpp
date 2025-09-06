// // // #pragma once

// // // #include <iostream>
// // // #include <functional>
// // // #include <map>
// // // #include <typeindex>
// // // #include <vector>
// // // #include <memory>

// // // class EventBus {
// // // private:
// // //     // A map to store subscribers.
// // //     // The key is the type of the event (e.g., typeid(TradeEvent)).
// // //     // The value is a vector of callback functions.
// // //     std::map<std::type_index, std::vector<std::function<void(const void*)>>> subscribers;

// // // public:
// // //     // Subscribes a handler to a specific event type.
// // //     template <typename EventType>
// // //     void subscribe(std::function<void(const EventType&)> handler) {
// // //         subscribers[std::type_index(typeid(EventType))].push_back(
// // //             [handler](const void* event_ptr) {
// // //                 // Cast the void pointer back to the correct event type
// // //                 handler(*static_cast<const EventType*>(event_ptr));
// // //             }
// // //         );
// // //     }

// // //     // Publishes an event to all subscribed handlers.
// // //     template <typename EventType>
// // //     void publish(const EventType& event) {
// // //         auto it = subscribers.find(std::type_index(typeid(EventType)));
// // //         if (it != subscribers.end()) {
// // //             for (const auto& handler : it->second) {
// // //                 handler(&event);
// // //             }
// // //         }
// // //     }
// // // };

// #pragma once
// #include <iostream>
// #include <map>
// #include <string>
// #include <functional>
// #include <typeindex>
// #include <memory>
// #include <any>
// #include <queue>
// #include <thread>
// #include <mutex>
// #include <condition_variable>
// #include "types.hpp"

// // Base class for all events
// struct Event {
//     virtual ~Event() = default;
// };

// struct TradeEvent : Event {
//     TradeData data;
// };

// struct OrderBookUpdateEvent : Event {
//     OrderBookUpdate data;
// };

// // Main EventBus class
// class EventBus {
// public:
//     template<typename EventType>
//     using Handler = std::function<void(const EventType&)>;

//     EventBus() {
//         worker_thread_ = std::make_unique<std::thread>(&EventBus::process_events, this);
//     }

//     ~EventBus() {
//         stop_ = true;
//         cv_.notify_one();
//         if (worker_thread_->joinable()) {
//             worker_thread_->join();
//         }
//     }

//     template<typename EventType>
//     void subscribe(Handler<EventType> handler) {
//         std::lock_guard<std::mutex> lock(handlers_mutex_);
//         handlers_[typeid(EventType)].push_back([handler](const std::shared_ptr<const Event>& e) {
//             handler(static_cast<const EventType&>(*e));
//         });
//     }

//     template<typename EventType>
//     void publish(const EventType& event) {
//         {
//             std::lock_guard<std::mutex> lock(queue_mutex_);
//             event_queue_.push({typeid(EventType), std::make_shared<EventType>(event)});
//         }
//         cv_.notify_one();
//     }

// private:
// // EventBus.hpp:
// void process_events() {
//     while (!stop_) {
//         std::pair<std::type_index, std::shared_ptr<const Event>> event_pair;
//         {
//             std::unique_lock<std::mutex> lock(queue_mutex_);
//             // The wait condition is correct
//             cv_.wait(lock, [this]{ return !event_queue_.empty() || stop_; });

//             // This is the CRITICAL part.
//             // If the thread was woken up because it needs to stop and the queue is empty, return.
//             if (stop_ && event_queue_.empty()) return;
            
//             // Get the pair from the queue and move it to the local variable.
//             event_pair = std::move(event_queue_.front()); 
//             event_queue_.pop();
//         }

//         // Lock for handlers only after popping from the queue
//         std::lock_guard<std::mutex> lock(handlers_mutex_);
//         auto it = handlers_.find(event_pair.first);
//         if (it != handlers_.end()) {
//             for (const auto& handler : it->second) {
//                 handler(event_pair.second);
//             }
//         }
//     }
// }
    
//     std::unique_ptr<std::thread> worker_thread_;
//     std::atomic<bool> stop_{false};

//     std::queue<std::pair<std::type_index, std::shared_ptr<const Event>>> event_queue_;
//     std::mutex queue_mutex_;
//     std::condition_variable cv_;

//     std::map<std::type_index, std::vector<std::function<void(const std::shared_ptr<const Event>&)>>> handlers_;
//     std::mutex handlers_mutex_;
// };

#pragma once
#include <iostream>
#include <map>
#include <string>
#include <functional>
#include <typeindex>
#include <memory>
#include <any>
#include "types.hpp"

class EventBus {
    public:
        template<typename EventType>
        using Handler = std::function<void(const EventType&)>;

        template<typename EventType>
        void subscribe(Handler<EventType> handler) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& handlers = handlers_[typeid(EventType)];
            if (handlers.empty()) {
                handlers.reserve(8); // Preallocate for 8 handlers
            }
            handlers.push_back([handler](const Event& e) {
                handler(static_cast<const EventType&>(e));
            });
        }

        template<typename EventType>
        void publish(EventType&& event) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handlers_.find(typeid(EventType));
            if (it != handlers_.end()) {
                for (const auto& handler : it->second) {
                    handler(event);
                }
            }
        }

    private:
        std::unordered_map<std::type_index, std::vector<std::function<void(const Event&)>>> handlers_;
        std::mutex mutex_;
};