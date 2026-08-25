#pragma once

#include "clipper2next/clipper.h"

#include <QVariant>

namespace clipper2next::demo {

[[nodiscard]] auto point_to_variant(const Point64& point) -> QVariantMap;
[[nodiscard]] auto path_to_variant(const Path64& path) -> QVariantList;
[[nodiscard]] auto paths_to_variant(const Paths64& paths) -> QVariantList;
[[nodiscard]] auto rect_to_variant(const Rect64& rect) -> QVariantMap;

}  // namespace clipper2next::demo
