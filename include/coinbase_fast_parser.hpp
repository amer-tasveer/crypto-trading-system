#pragma once

#include <cstring>
#include <cstdint>
#include <string_view>
#include <array>
#include <vector>
#include <utility>
#include "types.hpp"
#include "utils.hpp"

class CoinbaseFastParser {
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

    // Parse the depth update JSON
    static inline OrderBookData parse_depth_update(const char* json, size_t len) {
        OrderBookData result;
        const char* end = json + len;

        // Parse event time "time"
        const char* time_start = find_value_after_key(json, end, "time", 4);
        if (time_start) {
            result.timestamp = get_time_now_nano();

        } 

        // Parse symbol "product_id"
        const char* s_start = find_value_after_key(json, end, "product_id", 10);
        if (s_start) {
            const char* s_end = s_start;
            while (s_end < end && *s_end != '"') ++s_end;
            result.symbol = std::string_view(s_start, s_end - s_start);
        }

        // Parse changes "changes"
        const char* changes_start = find_value_after_key(json, end, "changes", 7);
        if (changes_start) {
            const char* changes_end = changes_start;
            int bracket_count = 1;
            while (changes_end < end && bracket_count > 0) {
                if (*changes_end == '[') ++bracket_count;
                if (*changes_end == ']') --bracket_count;
                ++changes_end;
            }

            // Iterate through the array of changes
            const char* current = changes_start;
            while (current < changes_end) {
                if (*current == '[') {
                    // Found the start of a new change entry
                    const char* type_start = current + 2; // Skip `["`
                    const char* type_end = type_start;
                    while (type_end < changes_end && *type_end != '"') ++type_end;

                    const char* price_start = type_end + 3; // Skip `", "`
                    const char* price_end = price_start;
                    while (price_end < changes_end && (*price_end >= '0' && *price_end <= '9' || *price_end == '.')) ++price_end;

                    const char* size_start = price_end + 3; // Skip `", "`
                    const char* size_end = size_start;
                    while (size_end < changes_end && (*size_end >= '0' && *size_end <= '9' || *size_end == '.')) ++size_end;

                    double price = parse_double(price_start, price_end);
                    double size = parse_double(size_start, size_end);
                    std::string_view type(type_start, type_end - type_start);

                    if (type == "buy") {
                        result.bids.push_back({price, size});
                    } else if (type == "sell") {
                        result.asks.push_back({price, size});
                    }

                    // Move to the next entry
                    current = size_end;
                    while (current < changes_end && *current != ']') ++current;
                    if (current < changes_end) ++current; // Skip the ']'
                } else {
                    ++current;
                }
            }
        }
        return result;
    }
};