#include <clipper2next/clip.h>
#include <clipper2next/geotypes/geotypes.hpp>

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
    return clipper2next::clip(request).closed.empty() ? 1 : 0;
}
