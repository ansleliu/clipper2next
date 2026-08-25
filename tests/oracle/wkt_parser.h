#pragma once

#include "external_corpus.h"

#include "clipper2next/clipper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clipper2next::tests::oracle {

namespace next = clipper2next;

[[nodiscard]] inline auto upper_ascii(std::string_view text) -> std::string {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return result;
}

inline auto skip_wkt_spaces(std::string_view text, std::size_t& offset) -> void {
    while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
        ++offset;
    }
}

[[nodiscard]] inline auto extract_parenthesized_groups(std::string_view text)
    -> std::vector<std::string_view> {
    std::vector<std::string_view> groups;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '(') {
            if (depth == 0) { start = index + 1U; }
            ++depth;
        } else if (text[index] == ')') {
            --depth;
            if (depth < 0) { throw std::runtime_error{"unbalanced WKT parentheses"}; }
            if (depth == 0) { groups.push_back(text.substr(start, index - start)); }
        }
    }
    if (depth != 0) { throw std::runtime_error{"unbalanced WKT parentheses"}; }
    return groups;
}

[[nodiscard]] inline auto parse_wkt_number(std::string_view text,
                                           std::size_t& offset,
                                           long double& value) -> bool {
    skip_wkt_spaces(text, offset);
    if (offset >= text.size()) { return false; }

    const auto begin = offset;
    if (text[offset] == '-' || text[offset] == '+') { ++offset; }
    auto has_digit = false;
    while (offset < text.size() && std::isdigit(static_cast<unsigned char>(text[offset])) != 0) {
        has_digit = true;
        ++offset;
    }
    if (offset < text.size() && text[offset] == '.') {
        ++offset;
        while (offset < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[offset])) != 0) {
            has_digit = true;
            ++offset;
        }
    }
    if (!has_digit) {
        offset = begin;
        return false;
    }
    if (offset < text.size() && (text[offset] == 'e' || text[offset] == 'E')) {
        const auto exponent = offset;
        ++offset;
        if (offset < text.size() && (text[offset] == '-' || text[offset] == '+')) { ++offset; }
        auto has_exponent_digit = false;
        while (offset < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[offset])) != 0) {
            has_exponent_digit = true;
            ++offset;
        }
        if (!has_exponent_digit) { offset = exponent; }
    }

    value = std::stold(std::string{text.substr(begin, offset - begin)});
    return true;
}

[[nodiscard]] inline auto scaled_coordinate(long double value, const wkt_parser_options& options)
    -> int64_t {
    return static_cast<int64_t>(std::llround(value * options.scale));
}

[[nodiscard]] inline auto parse_wkt_points(std::string_view points_text,
                                           const wkt_parser_options& options,
                                           bool trim_closure) -> next::Path64 {
    next::Path64 path;
    std::size_t offset = 0;
    while (offset < points_text.size()) {
        while (offset < points_text.size() &&
               (std::isspace(static_cast<unsigned char>(points_text[offset])) != 0 ||
                points_text[offset] == ',')) {
            ++offset;
        }
        if (offset >= points_text.size()) { break; }

        long double x = 0.0L;
        long double y = 0.0L;
        if (!parse_wkt_number(points_text, offset, x) ||
            !parse_wkt_number(points_text, offset, y)) {
            break;
        }
        path.push_back(next::Point64{scaled_coordinate(x, options), scaled_coordinate(y, options)});

        while (offset < points_text.size() && points_text[offset] != ',') {
            if (std::isspace(static_cast<unsigned char>(points_text[offset])) != 0) {
                ++offset;
                continue;
            }
            long double ignored = 0.0L;
            if (!parse_wkt_number(points_text, offset, ignored)) { ++offset; }
        }
    }

    if (trim_closure && path.size() > 1U && path.front() == path.back()) { path.pop_back(); }
    return path;
}

[[nodiscard]] inline auto parse_wkt_ring(std::string_view ring, const wkt_parser_options& options)
    -> next::Path64 {
    return parse_wkt_points(ring, options, true);
}

inline auto append_polygon_rings(std::string_view polygon_text,
                                 const wkt_parser_options& options,
                                 next::Paths64& paths) -> void {
    for (const auto ring : extract_parenthesized_groups(polygon_text)) {
        auto path = parse_wkt_ring(ring, options);
        if (path.size() >= 3U) { paths.push_back(std::move(path)); }
    }
}

[[nodiscard]] inline auto parse_polygonal_wkt(std::string_view wkt,
                                              const wkt_parser_options& options = {})
    -> next::Paths64 {
    const auto upper = upper_ascii(wkt);
    if (upper.find("EMPTY") != std::string::npos) { return {}; }

    next::Paths64 result;
    if (upper.rfind("POLYGON", 0) == 0) {
        const auto groups = extract_parenthesized_groups(wkt);
        if (groups.empty()) { return result; }
        append_polygon_rings(groups.front(), options, result);
        return result;
    }

    if (upper.rfind("MULTIPOLYGON", 0) == 0) {
        const auto groups = extract_parenthesized_groups(wkt);
        if (groups.empty()) { return result; }
        for (const auto polygon : extract_parenthesized_groups(groups.front())) {
            append_polygon_rings(polygon, options, result);
        }
        return result;
    }

    throw std::runtime_error{"unsupported polygonal WKT: " +
                             std::string{wkt.substr(0, std::min<std::size_t>(wkt.size(), 48U))}};
}

[[nodiscard]] inline auto parse_linear_wkt(std::string_view wkt,
                                           const wkt_parser_options& options = {})
    -> next::Paths64 {
    const auto upper = upper_ascii(wkt);
    if (upper.find("EMPTY") != std::string::npos) { return {}; }

    next::Paths64 result;
    if (upper.rfind("LINESTRING", 0) == 0) {
        const auto groups = extract_parenthesized_groups(wkt);
        if (groups.empty()) { return result; }
        auto path = parse_wkt_points(groups.front(), options, false);
        if (path.size() >= 2U) { result.push_back(std::move(path)); }
        return result;
    }

    if (upper.rfind("MULTILINESTRING", 0) == 0) {
        const auto groups = extract_parenthesized_groups(wkt);
        if (groups.empty()) { return result; }
        for (const auto line_text : extract_parenthesized_groups(groups.front())) {
            auto path = parse_wkt_points(line_text, options, false);
            if (path.size() >= 2U) { result.push_back(std::move(path)); }
        }
        return result;
    }

    throw std::runtime_error{"unsupported linear WKT: " +
                             std::string{wkt.substr(0, std::min<std::size_t>(wkt.size(), 48U))}};
}

}  // namespace clipper2next::tests::oracle
