#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <cstdint> 

inline std::string convert_milliseconds_to_timestamp(int64_t timestamp) {
    auto time_t = static_cast<std::time_t>(timestamp / 1000);
    auto ms = timestamp % 1000;
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms;
    return ss.str();
}

inline int64_t convert_timestamp_to_milliseconds(const std::string& timestamp_str) {
    std::tm t = {};
    std::istringstream ss(timestamp_str);
    
    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail()) {
        throw std::runtime_error("Failed to parse date and time string.");
    }
    
    long long milliseconds_part = 0;
    if (ss.peek() == '.') {
        ss.get(); // Read the decimal point
        std::string fractional_str;
        while (std::isdigit(ss.peek())) {
            fractional_str += ss.get();
        }
        
        // Convert to milliseconds and handle padding
        if (!fractional_str.empty()) {
            while (fractional_str.length() < 3) {
                fractional_str += '0';
            }
            if (fractional_str.length() > 3) {
                // Round to the nearest millisecond
                fractional_str = fractional_str.substr(0, 3);
            }
            milliseconds_part = std::stoll(fractional_str);
        }
    }
    
    std::time_t tt = std::mktime(&t);
    if (tt == -1) {
        throw std::runtime_error("Failed to convert tm to time_t.");
    }

    std::time_t local_time_now = std::time(nullptr);
    std::tm* tm_local = std::localtime(&local_time_now);
    
    std::time_t utc_time_now = std::mktime(tm_local);
    tm_local = std::gmtime(&local_time_now);
    utc_time_now = std::mktime(tm_local);
    
    // Calculate the difference in seconds
    int64_t offset_seconds = local_time_now - utc_time_now;
    
    // Subtract the offset to get the UTC timestamp
    tt += offset_seconds;
    
    return static_cast<int64_t>(tt) * 1000 + milliseconds_part;
}

inline int64_t get_time_now(){
    auto now = std::chrono::system_clock::now();

    // Get the duration since the epoch
    auto duration_since_epoch = now.time_since_epoch();

    // Cast the duration to microseconds
   return std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch).count();
}

inline double fast_stod(std::string_view s) {
    double integer_part = 0.0;
    double fractional_part = 0.0;
    double sign = 1.0;
    bool in_fraction = false;
    double fractional_divisor = 1.0;

    for (char c : s) {
        if (c == '-') {
            sign = -1.0;
        } else if (c == '.') {
            in_fraction = true;
        } else if (c >= '0' && c <= '9') {
            if (in_fraction) {
                fractional_divisor *= 10.0;
                fractional_part = (fractional_part * 10.0) + (c - '0');
            } else {
                integer_part = (integer_part * 10.0) + (c - '0');
            }
        }
    }
    return sign * (integer_part + (fractional_part / fractional_divisor));
}