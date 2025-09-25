#pragma once
#include <cstring>
#include <cstdint>
#include <string_view>
#include <array>
#include <vector>
#include <utility>
#include "types.hpp"
#include "utils.hpp"

class KrakenFastParser {
private:
    // Pre-computed powers of 10 for fast double conversion
    static constexpr std::array<double, 19> POWERS_OF_10 = {
        1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
    };

public:
    // Parse double from string
    static inline double parse_double(const char* start, const char* end) {
        const char* p = start;
        bool negative = false;
        if (*p == '-') {
            negative = true;
            ++p;
        }

        int64_t integer_part = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            integer_part = integer_part * 10 + (*p++ - '0');
        }

        double final_result = static_cast<double>(integer_part);

        if (p < end && *p == '.') {
            ++p;
            int64_t fractional_part = 0;
            const char* fraction_start = p;
            while (p < end && *p >= '0' && *p <= '9') {
                fractional_part = fractional_part * 10 + (*p++ - '0');
            }
            size_t num_digits = p - fraction_start;
            if (num_digits > 0 && num_digits < POWERS_OF_10.size()) {
                final_result += fractional_part / POWERS_OF_10[num_digits];
            }
        }

        return negative ? -final_result : final_result;
    }

    // Parse int64 from string
    static inline int64_t parse_int64(const char* start, const char* end) {
        int64_t result = 0;
        bool negative = false;
        const char* p = start;
        if (*p == '-') {
            negative = true;
            ++p;
        }
        while (p < end && *p >= '0' && *p <= '9') {
            result = result * 10 + (*p++ - '0');
        }
        return negative ? -result : result;
    }

    // Find value after a JSON key
    static inline const char* find_value_after_key(const char* start, const char* end,
                                                   const char* key, size_t key_len) {
        const char* current = start;
        while (current < end - key_len - 3) {
            current = static_cast<const char*>(memchr(current, '"', end - current));
            if (!current) return nullptr;

            if (current + key_len + 2 < end &&
                memcmp(current + 1, key, key_len) == 0 &&
                current[key_len + 1] == '"') {
                
                const char* value_start = current + key_len + 2;
                while (value_start < end && (*value_start == ' ' || *value_start == '\t')) {
                    ++value_start;
                }

                if (value_start < end && *value_start == ':') {
                    ++value_start;
                    while (value_start < end && (*value_start == ' ' || *value_start == '\t')) {
                        ++value_start;
                    }
                    if (value_start < end && *value_start == '"') {
                        return value_start + 1;
                    }
                    return value_start;
                }
            }

            const char* next_quote = static_cast<const char*>(memchr(current + 1, '"', end - (current + 1)));
            if (!next_quote) return nullptr;
            current = next_quote + 1;
        }
        return nullptr;
    }

    static inline std::vector<PriceLevel> parse_price_qty_array(const char* start, const char* end) {
        std::vector<PriceLevel> result;
        const char* p = start;
        
        // Skip whitespace and find opening bracket
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) ++p;
        if (p >= end || *p != '[') return result;
        ++p; // Skip opening bracket
        
        while (p < end) {
            // Skip whitespace
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) ++p;
            
            // Check for end of array
            if (p >= end || *p == ']') break;
            
            // Find object start
            if (*p == '{') {
                const char* obj_start = p;
                const char* obj_end = p;
                int brace_count = 1;
                ++obj_end;
                
                // Find matching closing brace
                while (obj_end < end && brace_count > 0) {
                    if (*obj_end == '{') ++brace_count;
                    else if (*obj_end == '}') --brace_count;
                    ++obj_end;
                }
                
                if (brace_count == 0) {
                    // Parse price and qty from this object
                    const char* price_val = find_value_after_key(obj_start, obj_end, "price", 5);
                    const char* qty_val = find_value_after_key(obj_start, obj_end, "qty", 3);
                    
                    if (price_val && qty_val) {
                        // Find end of price value
                        const char* price_end = price_val;
                        while (price_end < obj_end && (*price_end == '.' || *price_end == '-' || (*price_end >= '0' && *price_end <= '9'))) {
                            ++price_end;
                        }
                        
                        // Find end of qty value  
                        const char* qty_end = qty_val;
                        while (qty_end < obj_end && (*qty_end == '.' || *qty_end == '-' || (*qty_end >= '0' && *qty_end <= '9'))) {
                            ++qty_end;
                        }
                        
                        double price = parse_double(price_val, price_end);
                        double qty = parse_double(qty_val, qty_end);
                        
                        result.emplace_back(price, qty);
                    }
                }
                
                p = obj_end;
            }
            
            // Skip comma and whitespace
            while (p < end && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n')) ++p;
        }
        
        return result;
    }


    static inline int64_t parse_kraken_timestamp(const char* start, size_t len) {
        if (len < 20) return 0;
        int year, month, day, hour, minute, second;
        int nanoseconds = 0;
        
        sscanf(start, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);

        const char* dot = strchr(start, '.');
        if (dot) {
            int i = 0;
            char ns_str[10] = {0};
            const char* p = dot + 1;
            while (*p >= '0' && *p <= '9' && i < 9) ns_str[i++] = *p++;
            nanoseconds = atoi(ns_str);
            for (int j = i; j < 9; j++) nanoseconds *= 10;
        }

        std::tm t{};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = second;

        auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&t));
        auto duration_since_epoch = time_point.time_since_epoch();
        int64_t nanoseconds_total = std::chrono::duration_cast<std::chrono::nanoseconds>(duration_since_epoch).count() + nanoseconds;
        
        return nanoseconds_total;
    }
};