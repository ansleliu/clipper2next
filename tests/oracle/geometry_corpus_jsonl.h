#pragma once

#include <cstddef>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "clipper2next/core/point.h"
#include "clipper2next/core/rect.h"

namespace clipper2next::tests::oracle {

struct geometry_corpus_clip_request final {
    std::string operation{};
    std::string lhs_wkt{};
    std::string rhs_wkt{};
    std::string fill_rule{};
    bool preserve_collinear{};
    bool has_preserve_collinear{};
    bool reverse_solution{};
    bool has_reverse_solution{};
};

struct geometry_corpus_record final {
    std::string id{};
    std::string profile{};
    std::string operation{};
    std::string scenario{};
    std::string lhs_wkt{};
    std::string rhs_wkt{};
    std::string paths_wkt{};
    std::string lines_wkt{};
    std::string polygon_wkt{};
    std::string geometry_wkt{};
    std::string pattern_wkt{};
    std::string path_wkt{};
    std::string fill_rule{};
    std::string join_type{};
    std::string end_type{};
    std::string expected_wkt{};
    std::string expected_relation{};
    std::string expected_property{};
    std::string expected_geometry_type{};
    std::size_t expected_point_count{};
    clipper2next::Rect64 expected_bbox{};
    bool has_expected_bbox{};
    double delta{};
    bool has_delta{};
    double epsilon{};
    bool has_epsilon{};
    double scale_factor{};
    bool has_scale_factor{};
    int64_t delta_x{};
    bool has_delta_x{};
    int64_t delta_y{};
    bool has_delta_y{};
    clipper2next::Point64 point{};
    bool has_point{};
    bool is_closed{};
    bool has_is_closed{};
    bool preserve_collinear{};
    bool has_preserve_collinear{};
    bool reverse_solution{};
    bool has_reverse_solution{};
    clipper2next::Rect64 rect{};
    bool has_rect{};
    std::vector<geometry_corpus_clip_request> requests{};
};

[[nodiscard]] inline auto verification_profile_path(const std::filesystem::path& root,
                                                    std::string_view name)
    -> std::filesystem::path {
    return root / "normalized" / "verification" / (std::string{name} + ".jsonl");
}

[[nodiscard]] inline auto benchmark_profile_path(const std::filesystem::path& root,
                                                 std::string_view name)
    -> std::filesystem::path {
    return root / "normalized" / "benchmark" / (std::string{name} + ".jsonl");
}

// Controlled JSONL helper for compact corpus records produced by the external
// geometry corpus pipeline. This is intentionally narrow and not a general JSON parser.
[[nodiscard]] inline auto json_value_begin(std::string_view text, std::string_view key)
    -> std::size_t {
    int object_depth = 0;
    int array_depth = 0;
    for (std::size_t offset = 0; offset < text.size(); ++offset) {
        const auto ch = text[offset];
        if (ch == '{') {
            ++object_depth;
            continue;
        }
        if (ch == '}') {
            --object_depth;
            continue;
        }
        if (ch == '[') {
            ++array_depth;
            continue;
        }
        if (ch == ']') {
            --array_depth;
            continue;
        }
        if (ch != '"') { continue; }

        const auto string_begin = offset + 1U;
        bool escaped = false;
        ++offset;
        while (offset < text.size()) {
            if (escaped) {
                escaped = false;
            } else if (text[offset] == '\\') {
                escaped = true;
            } else if (text[offset] == '"') {
                break;
            }
            ++offset;
        }
        if (offset >= text.size() || object_depth != 1 || array_depth != 0 ||
            text.substr(string_begin, offset - string_begin) != key) {
            continue;
        }

        auto colon = offset + 1U;
        while (colon < text.size() &&
               (text[colon] == ' ' || text[colon] == '\t')) {
            ++colon;
        }
        if (colon >= text.size() || text[colon] != ':') { continue; }
        auto value_begin = colon + 1U;
        while (value_begin < text.size() &&
               (text[value_begin] == ' ' || text[value_begin] == '\t')) {
            ++value_begin;
        }
        return value_begin;
    }
    return std::string_view::npos;
}

[[nodiscard]] inline auto json_string_field(std::string_view text, std::string_view key)
    -> std::string {
    const auto begin = json_value_begin(text, key);
    if (begin == std::string_view::npos || begin >= text.size() || text[begin] != '"') {
        return {};
    }

    std::string value;
    auto offset = begin + 1U;
    while (offset < text.size() && text[offset] != '"') {
        if (text[offset] == '\\' && offset + 1U < text.size()) {
            ++offset;
        }
        value.push_back(text[offset]);
        ++offset;
    }
    return value;
}

[[nodiscard]] inline auto json_object_at(std::string_view text, std::size_t begin)
    -> std::string_view {
    if (begin == std::string_view::npos || begin >= text.size() || text[begin] != '{') {
        return {};
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (auto offset = begin; offset < text.size(); ++offset) {
        const auto ch = text[offset];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) { return text.substr(begin, offset - begin + 1U); }
        }
    }
    return {};
}

[[nodiscard]] inline auto json_object_field(std::string_view text, std::string_view key)
    -> std::string_view {
    return json_object_at(text, json_value_begin(text, key));
}

[[nodiscard]] inline auto json_array_field(std::string_view text, std::string_view key)
    -> std::string_view {
    const auto begin = json_value_begin(text, key);
    if (begin == std::string_view::npos || begin >= text.size() || text[begin] != '[') {
        return {};
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (auto offset = begin; offset < text.size(); ++offset) {
        const auto ch = text[offset];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) { return text.substr(begin, offset - begin + 1U); }
        }
    }
    return {};
}

[[nodiscard]] inline auto json_object_string_field(
    std::string_view text, std::string_view object_key, std::string_view key) -> std::string {
    return json_string_field(json_object_field(text, object_key), key);
}

[[nodiscard]] inline auto json_object_array(std::string_view array)
    -> std::vector<std::string_view> {
    std::vector<std::string_view> objects;
    if (array.empty() || array.front() != '[') { return objects; }

    std::size_t offset = 1U;
    while (offset < array.size()) {
        while (offset < array.size() &&
               (array[offset] == ' ' || array[offset] == '\t' || array[offset] == ',')) {
            ++offset;
        }
        if (offset >= array.size() || array[offset] == ']') { break; }
        const auto object = json_object_at(array, offset);
        if (object.empty()) { return {}; }
        objects.push_back(object);
        offset += object.size();
    }
    return objects;
}

[[nodiscard]] inline auto json_number_field(std::string_view text, std::string_view key)
    -> std::optional<double> {
    const auto begin = json_value_begin(text, key);
    if (begin == std::string_view::npos) { return std::nullopt; }
    auto end = begin;
    while (end < text.size() && text[end] != ',' && text[end] != '}') { ++end; }
    try {
        return std::stod(std::string{text.substr(begin, end - begin)});
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] inline auto json_bool_field(std::string_view text, std::string_view key)
    -> std::optional<bool> {
    const auto begin = json_value_begin(text, key);
    if (begin == std::string_view::npos) { return std::nullopt; }
    if (text.substr(begin, 4U) == "true") { return true; }
    if (text.substr(begin, 5U) == "false") { return false; }
    return std::nullopt;
}

[[nodiscard]] inline auto json_number_array(std::string_view array) -> std::vector<double> {
    std::vector<double> values;
    if (array.empty() || array.front() != '[') { return values; }

    std::size_t offset = 1U;
    while (offset < array.size()) {
        while (offset < array.size() &&
               (array[offset] == ' ' || array[offset] == '\t' || array[offset] == ',')) {
            ++offset;
        }
        if (offset >= array.size() || array[offset] == ']') { break; }

        const auto begin = offset;
        while (offset < array.size() && array[offset] != ',' && array[offset] != ']') {
            ++offset;
        }
        try {
            values.push_back(std::stod(std::string{array.substr(begin, offset - begin)}));
        } catch (...) {
            return {};
        }
    }
    return values;
}

[[nodiscard]] inline auto rounded_int64(double value) -> int64_t {
    return static_cast<int64_t>(std::llround(value));
}

inline auto parse_optional_rect(std::string_view inputs, geometry_corpus_record& record) -> void {
    const auto rect = json_object_field(inputs, "rect");
    if (rect.empty()) { return; }
    const auto left = json_number_field(rect, "left");
    const auto top = json_number_field(rect, "top");
    const auto right = json_number_field(rect, "right");
    const auto bottom = json_number_field(rect, "bottom");
    if (!left.has_value() || !top.has_value() || !right.has_value() || !bottom.has_value()) {
        return;
    }
    record.rect = clipper2next::Rect64{
        rounded_int64(*left),
        rounded_int64(*top),
        rounded_int64(*right),
        rounded_int64(*bottom),
    };
    record.has_rect = true;
}

inline auto parse_optional_point(std::string_view inputs, geometry_corpus_record& record) -> void {
    const auto point = json_object_field(inputs, "point");
    if (point.empty()) { return; }
    const auto x = json_number_field(point, "x");
    const auto y = json_number_field(point, "y");
    if (!x.has_value() || !y.has_value()) { return; }
    record.point = clipper2next::Point64{rounded_int64(*x), rounded_int64(*y)};
    record.has_point = true;
}

inline auto parse_optional_expected_bbox(std::string_view expected,
                                         geometry_corpus_record& record) -> void {
    const auto bbox = json_array_field(expected, "bbox");
    const auto values = json_number_array(bbox);
    if (values.size() != 4U) { return; }
    record.expected_bbox = clipper2next::Rect64{
        rounded_int64(values[0]),
        rounded_int64(values[1]),
        rounded_int64(values[2]),
        rounded_int64(values[3]),
    };
    record.has_expected_bbox = true;
}

[[nodiscard]] inline auto parse_geometry_corpus_jsonl(std::string_view text)
    -> std::vector<geometry_corpus_record> {
    std::vector<geometry_corpus_record> records;
    std::size_t line_begin = 0;
    while (line_begin < text.size()) {
        auto line_end = text.find('\n', line_begin);
        if (line_end == std::string_view::npos) { line_end = text.size(); }

        const auto line = std::string{text.substr(line_begin, line_end - line_begin)};
        if (line.empty()) {
            line_begin = line_end + 1U;
            continue;
        }
        geometry_corpus_record record;
        record.id = json_string_field(line, "id");
        record.profile = json_string_field(line, "profile");
        record.operation = json_string_field(line, "operation");
        record.scenario = json_string_field(line, "scenario");
        record.lhs_wkt = json_string_field(line, "lhs_wkt");
        record.rhs_wkt = json_string_field(line, "rhs_wkt");
        const auto inputs = json_object_field(line, "inputs");
        if (!inputs.empty()) {
            record.lhs_wkt = json_string_field(inputs, "lhs_wkt");
            record.rhs_wkt = json_string_field(inputs, "rhs_wkt");
            record.paths_wkt = json_string_field(inputs, "paths_wkt");
            record.lines_wkt = json_string_field(inputs, "lines_wkt");
            record.polygon_wkt = json_string_field(inputs, "polygon_wkt");
            record.geometry_wkt = json_string_field(inputs, "geometry_wkt");
            record.pattern_wkt = json_string_field(inputs, "pattern_wkt");
            record.path_wkt = json_string_field(inputs, "path_wkt");
            record.fill_rule = json_string_field(inputs, "fill_rule");
            record.join_type = json_string_field(inputs, "join_type");
            record.end_type = json_string_field(inputs, "end_type");
            if (const auto delta = json_number_field(inputs, "delta"); delta.has_value()) {
                record.delta = *delta;
                record.has_delta = true;
            }
            if (const auto epsilon = json_number_field(inputs, "epsilon");
                epsilon.has_value()) {
                record.epsilon = *epsilon;
                record.has_epsilon = true;
            }
            if (const auto scale_factor = json_number_field(inputs, "scale_factor");
                scale_factor.has_value()) {
                record.scale_factor = *scale_factor;
                record.has_scale_factor = true;
            }
            if (const auto delta_x = json_number_field(inputs, "delta_x");
                delta_x.has_value()) {
                record.delta_x = rounded_int64(*delta_x);
                record.has_delta_x = true;
            }
            if (const auto delta_y = json_number_field(inputs, "delta_y");
                delta_y.has_value()) {
                record.delta_y = rounded_int64(*delta_y);
                record.has_delta_y = true;
            }
            if (const auto is_closed = json_bool_field(inputs, "is_closed");
                is_closed.has_value()) {
                record.is_closed = *is_closed;
                record.has_is_closed = true;
            }
            if (const auto preserve = json_bool_field(inputs, "preserve_collinear");
                preserve.has_value()) {
                record.preserve_collinear = *preserve;
                record.has_preserve_collinear = true;
            }
            if (const auto reverse = json_bool_field(inputs, "reverse_solution");
                reverse.has_value()) {
                record.reverse_solution = *reverse;
                record.has_reverse_solution = true;
            }
            for (const auto request_json :
                 json_object_array(json_array_field(inputs, "requests"))) {
                geometry_corpus_clip_request request;
                request.operation = json_string_field(request_json, "operation");
                request.lhs_wkt = json_string_field(request_json, "lhs_wkt");
                request.rhs_wkt = json_string_field(request_json, "rhs_wkt");
                request.fill_rule = json_string_field(request_json, "fill_rule");
                if (const auto preserve =
                        json_bool_field(request_json, "preserve_collinear");
                    preserve.has_value()) {
                    request.preserve_collinear = *preserve;
                    request.has_preserve_collinear = true;
                }
                if (const auto reverse = json_bool_field(request_json, "reverse_solution");
                    reverse.has_value()) {
                    request.reverse_solution = *reverse;
                    request.has_reverse_solution = true;
                }
                record.requests.push_back(std::move(request));
            }
            parse_optional_rect(inputs, record);
            parse_optional_point(inputs, record);
        }
        record.expected_wkt = json_string_field(line, "expected_wkt");
        const auto expected = json_object_field(line, "expected");
        if (!expected.empty()) {
            if (record.expected_wkt.empty()) {
                record.expected_wkt = json_string_field(expected, "wkt");
            }
            record.expected_relation = json_string_field(expected, "relation");
            record.expected_property = json_string_field(expected, "property");
            record.expected_geometry_type = json_string_field(expected, "geometry_type");
            if (const auto point_count = json_number_field(expected, "point_count");
                point_count.has_value()) {
                record.expected_point_count = static_cast<std::size_t>(*point_count);
            }
            parse_optional_expected_bbox(expected, record);
        }
        if (!record.id.empty()) { records.push_back(std::move(record)); }

        line_begin = line_end + 1U;
    }
    return records;
}

}  // namespace clipper2next::tests::oracle
