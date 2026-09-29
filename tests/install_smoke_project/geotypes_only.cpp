#include <clipper2next/geotypes/geotypes.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

int main() {
    static_assert(sizeof(geotypes::Point2i64) == 16);
    static_assert(std::is_aggregate_v<geotypes::Point2i64>);
    const auto converted = geotypes::checkedCoordinateCast<std::int64_t>(1.5);
    if (!converted || *converted != 2) { return 1; }
    if (geotypes::checkedCoordinateCast<std::int64_t>(0x1p63)) { return 2; }
    const auto product = geotypes::checkedMultiply(geotypes::Point2i64{7, -3}, std::int64_t{2});
    if (!product || *product != geotypes::Point2i64{14, -6}) { return 3; }
    const auto points = std::array{geotypes::Point2i64{0, 0}, geotypes::Point2i64{1, 0}};
    const auto rings = std::array{geotypes::RingDescriptor{0, 2, geotypes::RingRole::Shell, {}}};
    const auto polygons = std::array{geotypes::PolygonDescriptor{0, 1, geotypes::noPolygonIndex}};
    auto scratch = std::array<std::uint8_t, 1>{};
    if (!geotypes::validateStructure(geotypes::TopologyView64{points, rings, polygons}, scratch)) {
        return 4;
    }
    return 0;
}
