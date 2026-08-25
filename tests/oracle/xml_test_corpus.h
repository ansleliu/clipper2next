#pragma once

#include "external_corpus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clipper2next::tests::oracle {

struct xml_overlay_operation final {
    std::string name{};
    overlay_operation kind{overlay_operation::intersection};
    std::string expected_wkt{};
};

struct xml_overlay_case final {
    std::string description{};
    std::string a_wkt{};
    std::string b_wkt{};
    std::vector<xml_overlay_operation> operations{};
};

[[nodiscard]] inline auto read_text_file(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path};
    if (!input) { throw std::runtime_error{"failed to open XML corpus at " + path.string()}; }
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] inline auto trim_xml_text(std::string_view text) -> std::string {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) { return {}; }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1U)};
}

[[nodiscard]] inline auto decode_xml_entities(std::string text) -> std::string {
    const auto replace_all = [&text](std::string_view from, std::string_view to) {
        std::size_t offset = 0;
        while ((offset = text.find(from, offset)) != std::string::npos) {
            text.replace(offset, from.size(), to);
            offset += to.size();
        }
    };
    replace_all("&lt;", "<");
    replace_all("&gt;", ">");
    replace_all("&quot;", "\"");
    replace_all("&apos;", "'");
    replace_all("&amp;", "&");
    return text;
}

[[nodiscard]] inline auto tag_text(std::string_view block, std::string_view name) -> std::string {
    const auto open = "<" + std::string{name};
    auto begin = block.find(open);
    if (begin == std::string_view::npos) { return {}; }
    begin = block.find('>', begin);
    if (begin == std::string_view::npos) { return {}; }
    ++begin;
    const auto close = "</" + std::string{name} + ">";
    const auto end = block.find(close, begin);
    if (end == std::string_view::npos) { return {}; }
    return decode_xml_entities(trim_xml_text(block.substr(begin, end - begin)));
}

[[nodiscard]] inline auto attribute_value(std::string_view tag, std::string_view name)
    -> std::string {
    const auto key = std::string{name} + "=";
    auto offset = tag.find(key);
    if (offset == std::string_view::npos) { return {}; }
    offset += key.size();
    if (offset >= tag.size() || (tag[offset] != '"' && tag[offset] != '\'')) { return {}; }
    const auto quote = tag[offset];
    ++offset;
    const auto end = tag.find(quote, offset);
    if (end == std::string_view::npos) { return {}; }
    return std::string{tag.substr(offset, end - offset)};
}

[[nodiscard]] inline auto is_supported_overlay_operation(std::string_view name) -> bool {
    return name == "intersection" || name == "union" || name == "difference" ||
           name == "symdifference" || name == "symDifference" || name == "xor";
}

inline auto append_xml_operations(std::string_view case_block, xml_overlay_case& test_case)
    -> void {
    std::size_t offset = 0;
    while ((offset = case_block.find("<op", offset)) != std::string_view::npos) {
        const auto tag_end = case_block.find('>', offset);
        if (tag_end == std::string_view::npos) { break; }
        const auto close = case_block.find("</op>", tag_end);
        if (close == std::string_view::npos) { break; }

        const auto tag = case_block.substr(offset, tag_end - offset + 1U);
        const auto name = attribute_value(tag, "name");
        if (is_supported_overlay_operation(name)) {
            xml_overlay_operation operation;
            operation.name = name;
            operation.kind = parse_overlay_operation(name);
            operation.expected_wkt = decode_xml_entities(
                trim_xml_text(case_block.substr(tag_end + 1U, close - tag_end - 1U)));
            test_case.operations.push_back(std::move(operation));
        }
        offset = close + 5U;
    }
}

[[nodiscard]] inline auto parse_xml_overlay_cases(std::string_view text)
    -> std::vector<xml_overlay_case> {
    std::vector<xml_overlay_case> cases;
    std::size_t offset = 0;
    while ((offset = text.find("<case", offset)) != std::string_view::npos) {
        const auto open_end = text.find('>', offset);
        if (open_end == std::string_view::npos) { break; }
        const auto close = text.find("</case>", open_end);
        if (close == std::string_view::npos) { break; }

        const auto block = text.substr(open_end + 1U, close - open_end - 1U);
        xml_overlay_case test_case;
        test_case.description = tag_text(block, "desc");
        test_case.a_wkt = tag_text(block, "a");
        test_case.b_wkt = tag_text(block, "b");
        append_xml_operations(block, test_case);
        if (!test_case.a_wkt.empty() && !test_case.b_wkt.empty() && !test_case.operations.empty()) {
            cases.push_back(std::move(test_case));
        }
        offset = close + 7U;
    }
    return cases;
}

[[nodiscard]] inline auto load_xml_overlay_cases(const std::filesystem::path& path)
    -> std::vector<xml_overlay_case> {
    return parse_xml_overlay_cases(read_text_file(path));
}

[[nodiscard]] inline auto load_xml_overlay_cases_recursively(const std::filesystem::path& root)
    -> std::vector<xml_overlay_case> {
    std::vector<xml_overlay_case> cases;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml") { continue; }
        auto file_cases = load_xml_overlay_cases(entry.path());
        cases.insert(cases.end(),
                     std::make_move_iterator(file_cases.begin()),
                     std::make_move_iterator(file_cases.end()));
    }
    return cases;
}

[[nodiscard]] inline auto count_supported_overlay_operations(
    const std::vector<xml_overlay_case>& cases) -> std::size_t {
    std::size_t count = 0;
    for (const auto& test_case : cases) { count += test_case.operations.size(); }
    return count;
}

}  // namespace clipper2next::tests::oracle
