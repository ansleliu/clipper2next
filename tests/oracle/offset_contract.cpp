#include "../support/offset_contract.h"

#include "geometry_corpus_jsonl.h"
#include "path_equivalence.h"
#include "clipper2/clipper.h"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace clipper2next::tests::oracle {
namespace {

// The formal source uses only exact integer point arrays. Parsing directly into
// int64 avoids a double round trip even for fixtures near the coordinate limit.
auto parse_paths(std::string_view value) -> Paths64 {
    Paths64 paths;
    int depth = 0;
    std::vector<std::int64_t> coordinates;
    for (std::size_t index = 0; index < value.size();) {
        const auto ch = value[index];
        if (ch == '[') {
            ++depth;
            if (depth == 2) { paths.emplace_back(); }
            if (depth == 3) { coordinates.clear(); }
            ++index;
        } else if (ch == ']') {
            if (depth == 3) {
                if (coordinates.size() != 2) { throw std::runtime_error{"invalid point"}; }
                paths.back().emplace_back(coordinates[0], coordinates[1]);
            }
            --depth;
            ++index;
        } else if (ch == '-' || (ch >= '0' && ch <= '9')) {
            std::int64_t number{};
            const auto parsed = std::from_chars(value.data() + index, value.data() + value.size(), number);
            if (parsed.ec != std::errc{} || depth != 3) { throw std::runtime_error{"invalid coordinate"}; }
            coordinates.push_back(number);
            index = static_cast<std::size_t>(parsed.ptr - value.data());
        } else if (ch == ',' || ch == ' ' || ch == '\t') {
            ++index;
        } else { throw std::runtime_error{"invalid paths"}; }
    }
    if (depth != 0) { throw std::runtime_error{"unclosed paths"}; }
    return paths;
}

auto join_name(JoinType value) -> std::string_view {
    switch (value) {
    case JoinType::Miter: return "miter";
    case JoinType::Bevel: return "bevel";
    case JoinType::Round: return "round";
    case JoinType::Square: return "square";
    }
    throw std::runtime_error{"invalid join"};
}

auto end_name(EndType value) -> std::string_view {
    switch (value) {
    case EndType::Polygon: return "polygon";
    case EndType::Joined: return "joined";
    case EndType::Butt: return "butt";
    case EndType::Round: return "round";
    case EndType::Square: return "square";
    }
    throw std::runtime_error{"invalid end"};
}

auto write_paths(std::ostream& output, const Paths64& paths) -> void {
    output << '[';
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index != 0) { output << ','; }
        output << '[';
        for (std::size_t point = 0; point < paths[index].size(); ++point) {
            if (point != 0) { output << ','; }
            output << '[' << paths[index][point].x << ',' << paths[index][point].y << ']';
        }
        output << ']';
    }
    output << ']';
}

auto callback_value(const std::string& callback) -> double {
    if (callback == "constant:10") { return 10; }
    if (callback == "constant:-10") { return -10; }
    throw std::runtime_error{"unknown formal callback"};
}

}  // namespace

auto load_offset_contract_cases(const std::filesystem::path& path)
    -> std::vector<offset_contract_case> {
    std::ifstream input{path};
    if (!input) { throw std::runtime_error{"missing Offset contract source: " + path.string()}; }
    std::vector<offset_contract_case> result;
    for (std::string line; std::getline(input, line);) {
        if (line.empty()) { continue; }
        offset_contract_case value;
        value.id = json_string_field(line, "id");
        value.contract_id = json_string_field(line, "contract_id");
        const auto inputs = json_object_field(line, "inputs");
        value.options.delta = json_number_field(inputs, "delta").value();
        value.options.miter_limit = json_number_field(inputs, "miter_limit").value();
        value.options.arc_tolerance = json_number_field(inputs, "arc_tolerance").value();
        value.options.arc_segments_per_quadrant = static_cast<std::size_t>(
            json_number_field(inputs, "arc_segments_per_quadrant").value());
        value.options.options.preserve_collinear = json_bool_field(inputs, "preserve_collinear").value();
        value.options.options.reverse_solution = json_bool_field(inputs, "reverse_solution").value();
        const auto rounding = json_string_field(inputs, "coordinate_rounding");
        if (rounding != "nearest_even" && rounding != "nearest_away_from_zero") {
            throw std::runtime_error{"invalid formal rounding"};
        }
        value.options.coordinate_rounding = rounding == "nearest_even"
            ? geotypes::CoordinateRounding::NearestEven : geotypes::CoordinateRounding::NearestAwayFromZero;
        value.callback = json_string_field(inputs, "callback");
        if (value.callback != "none") { static_cast<void>(callback_value(value.callback)); }
        for (const auto group_json : json_object_array(json_array_field(inputs, "groups"))) {
            offset_contract_group group;
            group.paths = parse_paths(json_array_field(group_json, "paths"));
            const auto join = json_string_field(group_json, "join_type");
            const auto end = json_string_field(group_json, "end_type");
            bool join_found = false;
            bool end_found = false;
            for (const auto candidate : {JoinType::Miter, JoinType::Bevel, JoinType::Round, JoinType::Square}) {
                if (join_name(candidate) == join) { group.join_type = candidate; join_found = true; }
            }
            for (const auto candidate : {EndType::Polygon, EndType::Joined, EndType::Butt, EndType::Round, EndType::Square}) {
                if (end_name(candidate) == end) { group.end_type = candidate; end_found = true; }
            }
            if (!join_found || !end_found) { throw std::runtime_error{"invalid formal group"}; }
            value.groups.emplace_back(std::move(group));
        }
        if (value.groups.empty()) { throw std::runtime_error{"missing formal groups"}; }
        result.emplace_back(std::move(value));
    }
    return result;
}

auto write_offset_contract_record(std::ostream& output, const std::size_t case_index,
                                  const offset_contract_case& request,
                                  const clipper_error_code outer_error, const Paths64& actual_paths,
                                  const bool legacy) -> void {
    const auto& options = request.options;
    output << (legacy ? "OFFSET_LEGACY " : "OFFSET_CONTRACT ") << std::setprecision(17)
           << "{\"case_index\":" << case_index << ",\"contract_id\":\"" << request.contract_id
           << "\",\"inputs\":{\"delta\":" << options.delta
           << ",\"miter_limit\":" << options.miter_limit
           << ",\"arc_tolerance\":" << options.arc_tolerance
           << ",\"arc_segments_per_quadrant\":" << options.arc_segments_per_quadrant
           << ",\"preserve_collinear\":" << (options.options.preserve_collinear ? "true" : "false")
           << ",\"reverse_solution\":" << (options.options.reverse_solution ? "true" : "false")
           << ",\"coordinate_rounding\":\""
           << (options.coordinate_rounding == geotypes::CoordinateRounding::NearestEven
                   ? "nearest_even" : "nearest_away_from_zero")
           << "\",\"callback\":\"" << request.callback << "\",\"groups\":[";
    for (std::size_t index = 0; index < request.groups.size(); ++index) {
        if (index != 0) { output << ','; }
        const auto& group = request.groups[index];
        output << "{\"join_type\":\"" << join_name(group.join_type) << "\",\"end_type\":\""
               << end_name(group.end_type) << "\",\"paths\":";
        write_paths(output, group.paths);
        output << '}';
    }
    output << "]},\"outer_error\":\"" << (outer_error == clipper_error_code::ok ? "ok" : "error")
           << "\",\"paths\":";
    write_paths(output, actual_paths);
    output << "}\n";
}

auto execute_offset_contract_case(const offset_contract_case& request) -> Paths64 {
    if (request.callback != "none") {
        // Public callbacks are a builder contract; reject unrepresentable options.
        if (request.options.arc_segments_per_quadrant != 0 ||
            request.options.coordinate_rounding != geotypes::CoordinateRounding::NearestEven) {
            throw std::runtime_error{"builder cannot represent formal callback options"};
        }
        const auto delta = callback_value(request.callback);
        auto builder = offset_builder{};
        builder.delta(request.options.delta).miter_limit(request.options.miter_limit)
            .arc_tolerance(request.options.arc_tolerance).options(request.options.options)
            .delta_callback([delta](const auto&, const auto&, auto, auto) { return delta; });
        for (const auto& group : request.groups) {
            builder.join(group.join_type).end(group.end_type).add(group.paths);
        }
        return builder.execute();
    }
    std::vector<borrowed_offset_group64> groups;
    for (const auto& group : request.groups) {
        groups.push_back({borrow_paths64(group.paths), group.join_type, group.end_type});
    }
    borrowed_offset_request64 operation;
    operation.groups = groups;
    operation.delta = request.options.delta;
    operation.miter_limit = request.options.miter_limit;
    operation.arc_tolerance = request.options.arc_tolerance;
    operation.arc_segments_per_quadrant = request.options.arc_segments_per_quadrant;
    operation.coordinate_rounding = request.options.coordinate_rounding;
    operation.options = request.options.options;
    const auto result = offset_stage_checked(operation);
    if (!result) { raise_clipper_error(result.error()); }
    Paths64 paths;
    for (const auto path : result->paths) { paths.emplace_back(path.begin(), path.end()); }
    return paths;
}

auto execute_legacy_offset_contract_case(const offset_contract_case& request) -> Paths64 {
    Clipper2Lib::ClipperOffset operation{request.options.miter_limit, request.options.arc_tolerance,
        request.options.options.preserve_collinear, request.options.options.reverse_solution};
    for (const auto& group : request.groups) {
        // Both public enum declarations have the same explicit order.
        operation.AddPaths(to_legacy_paths(group.paths), static_cast<Clipper2Lib::JoinType>(group.join_type),
                           static_cast<Clipper2Lib::EndType>(group.end_type));
    }
    if (request.callback != "none") {
        const auto delta = callback_value(request.callback);
        operation.SetDeltaCallback([delta](const auto&, const auto&, auto, auto) { return delta; });
    }
    Clipper2Lib::Paths64 paths;
    operation.Execute(request.options.delta, paths);
    if (operation.ErrorCode() != 0) { throw std::runtime_error{"legacy Offset execution failed"}; }
    return to_next_paths(paths);
}

auto record_offset_contract_cases(std::ostream& output, const std::filesystem::path& source) -> std::size_t {
    const auto cases = load_offset_contract_cases(source);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto actual = execute_offset_contract_case(cases[index]);
        const auto legacy_actual = execute_legacy_offset_contract_case(cases[index]);
        write_offset_contract_record(output, index, cases[index], clipper_error_code::ok, actual);
        write_offset_contract_record(output, index, cases[index], clipper_error_code::ok, legacy_actual, true);
    }
    return cases.size();
}

}  // namespace clipper2next::tests::oracle
