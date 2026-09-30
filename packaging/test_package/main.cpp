#include <clipper2next/clip.h>
#include <clipper2next/geotypes/geotypes.hpp>
#include <clipper2next/offset.h>

#include <limits>

int main() {
    static_assert(sizeof(geotypes::Point2i64) == 16U);
    const auto subjects = clipper2next::Paths64{
        clipper2next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
    };
    const auto clips = clipper2next::Paths64{
        clipper2next::Path64{{5, 5}, {15, 5}, {15, 15}, {5, 15}},
    };
    auto request = clipper2next::clip_request64{};
    request.clip_type = clipper2next::ClipType::Intersection;
    request.fill_rule = clipper2next::FillRule::NonZero;
    request.subjects = subjects;
    request.clips = clips;
    if (clipper2next::clip(request).closed.empty()) { return 1; }
    const auto coordinate = geotypes::checkedCoordinateCast<std::int64_t>(1.5);
    if (!coordinate || *coordinate != 2) { return 2; }
    if (geotypes::checkedCoordinateCast<std::int64_t>(0x1p63)) { return 3; }
    auto offset_request = clipper2next::offset_request64{};
    offset_request.paths = subjects;
    offset_request.delta = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = clipper2next::offset_checked(offset_request);
    if (invalid || invalid.error() != clipper2next::clipper_error_code::invalid_argument) {
        return 4;
    }
    offset_request.paths = {{{0, 0}}};
    offset_request.delta = 0x1p63;
    offset_request.join_type = clipper2next::JoinType::Round;
    offset_request.end_type = clipper2next::EndType::Round;
    offset_request.arc_segments_per_quadrant = 8U;
    const auto range_failure = clipper2next::offset_checked(offset_request);
    if (range_failure || range_failure.error() != clipper2next::clipper_error_code::coordinate_range) {
        return 5;
    }
    const auto group = clipper2next::borrowed_offset_group64{
        clipper2next::borrow_paths64(offset_request.paths),
        clipper2next::JoinType::Round, clipper2next::EndType::Round};
    auto borrowed = clipper2next::borrowed_offset_request64{};
    borrowed.groups = std::span{&group, 1U};
    borrowed.delta = offset_request.delta;
    borrowed.arc_segments_per_quadrant = offset_request.arc_segments_per_quadrant;
    const auto borrowed_failure = clipper2next::offset_stage_checked(borrowed);
    if (borrowed_failure || borrowed_failure.error() != clipper2next::clipper_error_code::coordinate_range) {
        return 6;
    }
    return 0;
}
