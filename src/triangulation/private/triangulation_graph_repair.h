#pragma once
#include "clipper2next/triangulation/request.h"
#include "triangulation/private/triangulation_context.h"
#include "support/private/checked_size.h"
#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace triangulation_graph_repair_detail {
struct containment_relation {
    std::size_t ring;
    int value = -1;
    bool contact = false;
};
using indexed_relation = std::pair<std::size_t, containment_relation>;

// One relation table owns all rows. Offsets delimit sorted neighbours without
// a separate allocation for every contour in a sparse containment graph.
class containment_rows final {
public:
    containment_rows(std::size_t count, std::span<const indexed_relation> candidates)
        : offsets_(checked_size_add(count, 1U)) {
        for (const auto& candidate : candidates) {
            ++offsets_[candidate.first];
        }
        auto end = std::size_t{};
        for (std::size_t ring = 0; ring < count; ++ring) {
            end = checked_size_add(end, offsets_[ring]);
            offsets_[ring] = end;
        }
        offsets_.back() = end;
        values_.resize(end);
        // Filling backwards turns each exclusive row end into its row start.
        for (const auto& [ring, relation] : candidates) {
            values_[--offsets_[ring]] = relation;
        }
        for (std::size_t ring = 0; ring < count; ++ring) {
            const auto row = (*this)[ring];
            std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) {
                return a.ring < b.ring;
            });
        }
    }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return offsets_.size() - 1U; }
    [[nodiscard]] auto operator[](std::size_t ring) noexcept -> std::span<containment_relation> {
        return std::span{values_}.subspan(offsets_[ring], offsets_[ring + 1U] - offsets_[ring]);
    }
    [[nodiscard]] auto operator[](std::size_t ring) const noexcept -> std::span<const containment_relation> {
        return std::span{values_}.subspan(offsets_[ring], offsets_[ring + 1U] - offsets_[ring]);
    }
private:
    std::vector<std::size_t> offsets_;
    std::vector<containment_relation> values_;
};
}  // namespace triangulation_graph_repair_detail

[[nodiscard]] auto normalize_triangulation_constraints(triangulation_context& context)
    -> TriangulateResult;
}
