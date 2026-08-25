#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>

namespace clipper2next::tests::oracle {

[[nodiscard]] inline auto geometry_corpus_root() -> std::string {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, "CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT") == 0 &&
        value != nullptr) {
        std::string result{value};
        std::free(value);
        if (!result.empty()) { return result; }
    }
#else
    if (const auto* value = std::getenv("CLIPPER2NEXT_GEOMETRY_CORPUS_ROOT");
        value != nullptr && value[0] != '\0') {
        return value;
    }
#endif

#if defined(CLIPPER2NEXT_DEFAULT_GEOMETRY_CORPUS_ROOT)
    return CLIPPER2NEXT_DEFAULT_GEOMETRY_CORPUS_ROOT;
#else
    return {};
#endif
}

}  // namespace clipper2next::tests::oracle
