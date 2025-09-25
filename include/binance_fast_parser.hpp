#pragma once

#include <simdjson.h>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <array>
#include <vector>
#include <utility>
#include "types.hpp"
#include "utils.hpp"

class BinanceFastParser {
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

    // Parse array of [price, quantity] pairs
    static inline std::vector<std::pair<double, double>> parse_array(const char* start, const char* end) {
        std::vector<std::pair<double, double>> result;
        const char* p = start;
        
        // Skip opening bracket
        if (p < end && *p == '[') ++p;
        
        while (p < end && *p != ']') {
            // Skip whitespace
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) ++p;
            
            // Expect opening bracket of pair
            if (p < end && *p == '[') ++p;
            
            // Parse price
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            const char* price_start = p;
            if (p < end && *p == '"') ++p;
            while (p < end && *p != '"') ++p;
            double price = parse_double(price_start + 1, p);
            if (p < end && *p == '"') ++p;
            
            // Skip comma and whitespace
            while (p < end && (*p == ',' || *p == ' ' || *p == '\t')) ++p;
            
            // Parse quantity
            const char* qty_start = p;
            if (p < end && *p == '"') ++p;
            while (p < end && *p != '"') ++p;
            double quantity = parse_double(qty_start + 1, p);
            if (p < end && *p == '"') ++p;
            
            result.emplace_back(price, quantity);
            
            // Skip closing bracket and comma
            while (p < end && (*p == ']' || *p == ',' || *p == ' ' || *p == '\t' || *p == '\n')) ++p;
        }
        
        return result;
    }

    // Parse the depth update JSON
    static inline OrderBookData parse_depth_update(const char* json, size_t len) {
        OrderBookData result;
        const char* end = json + len;

        // Parse event type "e"
        // const char* e_start = find_value_after_key(json, end, "e", 1);
        // if (e_start) {
        //     const char* e_end = e_start;
        //     while (e_end < end && *e_end != '"') ++e_end;
        //     result.event_type = std::string_view(e_start, e_end - e_start);
        // }

        // Parse event time "E"
        const char* E_start = find_value_after_key(json, end, "E", 1);
        if (E_start) {
            const char* E_end = E_start;
            while (E_end < end && (*E_end >= '0' && *E_end <= '9')) ++E_end;
            // result.event_time = parse_int64(E_start, E_end);
            result.timestamp = get_time_now_nano();
        
        }

        // Parse symbol "s"
        const char* s_start = find_value_after_key(json, end, "s", 1);
        if (s_start) {
            const char* s_end = s_start;
            while (s_end < end && *s_end != '"') ++s_end;
            result.symbol = std::string_view(s_start, s_end - s_start);
        }

        // Parse first update ID "U"
        const char* U_start = find_value_after_key(json, end, "U", 1);
        if (U_start) {
            const char* U_end = U_start;
            while (U_end < end && (*U_end >= '0' && *U_end <= '9')) ++U_end;
            result.id = parse_int64(U_start, U_end);
        }

        // Parse final update ID "u"
        const char* u_start = find_value_after_key(json, end, "u", 1);
        if (u_start) {
            const char* u_end = u_start;
            while (u_end < end && (*u_end >= '0' && *u_end <= '9')) ++u_end;
            result.id = parse_int64(u_start, u_end);
        }

        // Parse bids "b"
        const char* b_start = find_value_after_key(json, end, "b", 1);
        if (b_start) {
            const char* b_end = b_start;
            int bracket_count = 1;
            while (b_end < end && bracket_count > 0) {
                if (*b_end == '[') ++bracket_count;
                if (*b_end == ']') --bracket_count;
                ++b_end;
            }
            result.bids = parse_array(b_start, b_end);
        }

        // Parse asks "a"
        const char* a_start = find_value_after_key(json, end, "a", 1);
        if (a_start) {
            const char* a_end = a_start;
            int bracket_count = 1;
            while (a_end < end && bracket_count > 0) {
                if (*a_end == '[') ++bracket_count;
                if (*a_end == ']') --bracket_count;
                ++a_end;
            }
            result.asks = parse_array(a_start, a_end);
        }

        return result;
    }

    static inline TickerData parse_ticker(const char* json, size_t len) {
        
    }

};