#pragma once

#include <initializer_list>

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"
#include "../../tests/oracle/path_equivalence.h"

namespace clipper2next::benchmarks {

namespace legacy = Clipper2Lib;
namespace next = clipper2next;

struct clip_fixture final {
    legacy::Paths64 legacy_subject;
    legacy::Paths64 legacy_clip;
    next::Paths64 next_subject;
    next::Paths64 next_clip;
};

struct offset_fixture final {
    legacy::Paths64 legacy_subject;
    next::Paths64 next_subject;
};

struct rectclip_fixture final {
    legacy::Rect64 legacy_rect;
    legacy::Paths64 legacy_subject;
    next::Rect64 next_rect;
    next::Paths64 next_subject;
};

inline auto make_next_path(std::initializer_list<int64_t> coordinates) -> next::Path64 {
    next::Path64 path;
    auto current = coordinates.begin();
    while (current != coordinates.end()) {
        const auto x = *current++;
        const auto y = *current++;
        path.emplace_back(x, y);
    }
    return path;
}

inline auto make_clip_fixture() -> clip_fixture {
    return {
        {legacy::MakePath({0, 0, 1000, 0, 1000, 1000, 0, 1000})},
        {legacy::MakePath({250, 250, 1250, 250, 1250, 1250, 250, 1250})},
        {make_next_path({0, 0, 1000, 0, 1000, 1000, 0, 1000})},
        {make_next_path({250, 250, 1250, 250, 1250, 1250, 250, 1250})},
    };
}

inline auto make_offset_fixture() -> offset_fixture {
    return {
        {legacy::MakePath({0, 0, 500, 0, 650, 300, 300, 650, 0, 500})},
        {make_next_path({0, 0, 500, 0, 650, 300, 300, 650, 0, 500})},
    };
}

inline auto make_rectclip_fixture() -> rectclip_fixture {
    return {
        legacy::Rect64{100, 100, 900, 900},
        {legacy::MakePath({0, 0, 1000, 50, 950, 1000, 50, 950})},
        next::Rect64{100, 100, 900, 900},
        {make_next_path({0, 0, 1000, 50, 950, 1000, 50, 950})},
    };
}

inline auto assert_same_paths(const legacy::Paths64& expected, const next::Paths64& actual)
    -> void {
    clipper2next::tests::oracle::assert_paths_semantically_equal(expected, actual);
}

}  // namespace clipper2next::benchmarks
