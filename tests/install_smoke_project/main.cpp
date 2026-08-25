#include <clipper2next/clip.h>
#include <clipper2next/geotypes/geotypes.hpp>
#include <clipper2next/minkowski.h>
#include <clipper2next/offset.h>
#include <clipper2next/rectclip.h>
#include <clipper2next/triangulation.h>

struct topology_smoke_sink final {
    auto begin(const clipper2next::topology_layout64& layout)
        -> clipper2next::clipper_error_code {
        expected_ring_count = layout.ring_count;
        return clipper2next::clipper_error_code::ok;
    }

    auto acquire(const clipper2next::topology_ring_layout64& ring,
                 std::span<geotypes::Point2i64>& destination)
        -> clipper2next::clipper_error_code {
        points.resize(ring.point_count);
        destination = points;
        ++ring_count;
        return clipper2next::clipper_error_code::ok;
    }

    auto finish() -> clipper2next::clipper_error_code {
        return ring_count == expected_ring_count ? clipper2next::clipper_error_code::ok
                                                 : clipper2next::clipper_error_code::sink_failure;
    }

    auto cancel() noexcept -> void {}

    std::size_t expected_ring_count{};
    std::size_t ring_count{};
    std::vector<geotypes::Point2i64> points{};
};

int main() {
    static_assert(sizeof(geotypes::Point2i64) == 16U);
    const auto shared_point = geotypes::Point2i64{3, 4};
    if (shared_point.x != 3 || shared_point.y != 4) { return 16; }

    const auto paths = clipper2next::Paths64{
        clipper2next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
    };
    const auto clips = clipper2next::Paths64{
        clipper2next::Path64{{5, 5}, {15, 5}, {15, 15}, {5, 15}},
    };
    clipper2next::clip_request64 clip_request;
    clip_request.subjects = paths;
    clip_request.clips = clips;
    clip_request.fill_rule = clipper2next::FillRule::NonZero;

    clip_request.clip_type = clipper2next::ClipType::Union;
    const auto union_result = clipper2next::clip(clip_request);
    clip_request.clip_type = clipper2next::ClipType::Intersection;
    const auto intersect_result = clipper2next::clip(clip_request);
    clip_request.clip_type = clipper2next::ClipType::Difference;
    const auto difference_result = clipper2next::clip(clip_request);
    clip_request.clip_type = clipper2next::ClipType::Xor;
    const auto xor_result = clipper2next::clip(clip_request);

    clipper2next::offset_request64 offset_request;
    offset_request.paths = paths;
    offset_request.delta = 2.0;
    offset_request.join_type = clipper2next::JoinType::Miter;
    offset_request.end_type = clipper2next::EndType::Polygon;
    const auto offset_result = clipper2next::offset(offset_request);
    auto borrowed_offset_request = clipper2next::borrowed_offset_request64{};
    borrowed_offset_request.paths = clipper2next::borrow_paths64(paths);
    borrowed_offset_request.delta = 2.0;
    const auto borrowed_offset_result =
        clipper2next::offset_stage_checked(borrowed_offset_request);

    clipper2next::rect_clip_request64 rectclip_request;
    rectclip_request.rect = clipper2next::Rect64{0, 0, 8, 8};
    rectclip_request.paths = paths;
    const auto rectclip_result = clipper2next::rect_clip(rectclip_request);
    clipper2next::minkowski_request64 minkowski_request;
    minkowski_request.pattern = paths.front();
    minkowski_request.path = clips.front();
    minkowski_request.is_closed = true;
    const auto minkowski_result = clipper2next::minkowski_sum(minkowski_request);

    clipper2next::triangulation_request64 triangulation_request;
    triangulation_request.paths = paths;
    triangulation_request.use_delaunay = false;
    const auto triangulate_result = clipper2next::triangulate(triangulation_request);

    auto topology_sink = topology_smoke_sink{};
    auto topology_request = clipper2next::borrowed_clip_request64{};
    topology_request.clip_type = clipper2next::ClipType::Union;
    topology_request.fill_rule = clipper2next::FillRule::NonZero;
    topology_request.subjects = clipper2next::borrow_paths64(paths);
    const auto topology_result = clipper2next::clip_topology_checked(
        topology_request, clipper2next::make_topology_writer64(topology_sink));

    if (union_result.closed.empty()) { return 1; }
    if (intersect_result.closed.empty()) { return 6; }
    if (difference_result.closed.empty()) { return 7; }
    if (xor_result.closed.empty()) { return 8; }
    if (offset_result.closed.empty()) { return 5; }
    if (!borrowed_offset_result.has_value() ||
        borrowed_offset_result->paths.empty() ||
        borrowed_offset_result->stats.input_collection_point_writes != 0U) {
        return 15;
    }
    if (rectclip_result.paths.empty()) { return 11; }
    if (minkowski_result.empty()) { return 12; }
    if (triangulate_result.status != clipper2next::TriangulateResult::success ||
        triangulate_result.triangles.empty()) {
        return 13;
    }
    if (!topology_result.has_value() || topology_sink.ring_count == 0U) { return 14; }
    return 0;
}
