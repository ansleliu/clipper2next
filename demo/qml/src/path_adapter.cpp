#include "path_adapter.h"

namespace clipper2next::demo {

auto point_to_variant(const Point64& point) -> QVariantMap {
    return QVariantMap{
        {"x", QVariant::fromValue(static_cast<double>(point.x))},
        {"y", QVariant::fromValue(static_cast<double>(point.y))},
    };
}

auto path_to_variant(const Path64& path) -> QVariantList {
    QVariantList points;
    points.reserve(static_cast<qsizetype>(path.size()));
    for (const auto& point : path) { points.push_back(point_to_variant(point)); }
    return points;
}

auto paths_to_variant(const Paths64& paths) -> QVariantList {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(paths.size()));
    for (const auto& path : paths) { result.push_back(path_to_variant(path)); }
    return result;
}

auto rect_to_variant(const Rect64& rect) -> QVariantMap {
    return QVariantMap{
        {"left", QVariant::fromValue(static_cast<double>(rect.left))},
        {"top", QVariant::fromValue(static_cast<double>(rect.top))},
        {"right", QVariant::fromValue(static_cast<double>(rect.right))},
        {"bottom", QVariant::fromValue(static_cast<double>(rect.bottom))},
    };
}

}  // namespace clipper2next::demo
