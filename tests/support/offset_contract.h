#pragma once

#include "clipper2next/clipper.h"

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace clipper2next::tests::oracle {

struct offset_contract_group final {
    Paths64 paths;
    JoinType join_type{JoinType::Miter};
    EndType end_type{EndType::Polygon};
};

struct offset_contract_case final {
    std::string id;
    std::string contract_id;
    std::vector<offset_contract_group> groups;
    offset_request64 options;
    std::string callback{"none"};
};

[[nodiscard]] auto load_offset_contract_cases(const std::filesystem::path& path)
    -> std::vector<offset_contract_case>;

// Records the request actually executed and the output returned by that execution.
// It neither computes expected geometry nor reruns an engine.
auto write_offset_contract_record(std::ostream& output, std::size_t case_index,
                                  const offset_contract_case& request,
                                  clipper_error_code outer_error, const Paths64& actual_paths,
                                  bool legacy = false) -> void;

[[nodiscard]] auto record_offset_contract_cases(std::ostream& output, const std::filesystem::path& source)
    -> std::size_t;

[[nodiscard]] auto execute_offset_contract_case(const offset_contract_case& request)
    -> Paths64;
[[nodiscard]] auto execute_legacy_offset_contract_case(const offset_contract_case& request)
    -> Paths64;

}  // namespace clipper2next::tests::oracle
