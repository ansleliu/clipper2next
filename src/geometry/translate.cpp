#include "clipper2next/geometry/translate.h"

namespace clipper2next {

auto translate(const Path64& path, int64_t dx, int64_t dy) -> Path64 {
    return translate<int64_t>(path, dx, dy);
}

auto translate(const PathD& path, double dx, double dy) -> PathD {
    return translate<double>(path, dx, dy);
}

auto translate(const Paths64& paths, int64_t dx, int64_t dy) -> Paths64 {
    return translate<int64_t>(paths, dx, dy);
}

auto translate(const PathsD& paths, double dx, double dy) -> PathsD {
    return translate<double>(paths, dx, dy);
}

}  // namespace clipper2next
