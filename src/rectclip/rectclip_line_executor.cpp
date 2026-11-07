#include "rectclip/private/rectclip_line_executor.h"

#include "rectclip/private/rectclip_classifier.h"
#include "rectclip/private/rectclip_intersections.h"
#include "rectclip/private/rectclip_traversal.h"

namespace {
using Location = clipper2next::internal::rect_location;

// A line contour is an ordered point sequence. Publish only complete contours
// to this operation's private result; no polygon node graph is needed.
class line_output final {
public:
    explicit line_output(clipper2next::Paths64& paths) noexcept : paths_(paths) {}
    line_output(const line_output&) = delete;
    auto operator=(const line_output&) -> line_output& = delete;
    ~line_output() { finish(); }
    auto add(clipper2next::Point64 point, bool start_new = false) -> void {
        if (start_new) { finish(); }
        if (started_) {
            auto& path = paths_.back();
            if (path.back() != point) { path.emplace_back(point); }
        } else if (!pending_) {
            first_ = point;
            pending_ = true;
        } else if (first_ != point) {
            // A contour first becomes a result at its second distinct point.
            start_path(2U).emplace_back(point);
        }
    }
    auto add(std::span<const clipper2next::Point64> points) -> void {
        if (points.empty()) { return; }
        if (!started_) {
            if (!pending_) {
                first_ = points.front();
                pending_ = true;
                points = points.subspan(1U);
            }
            while (!points.empty() && points.front() == first_) {
                points = points.subspan(1U);
            }
            if (points.empty()) { return; }
            // One pending endpoint, this contiguous interior run and at most
            // one exiting intersection belong to the current contour.
            start_path(points.size() + 2U);
        } else {
            auto& path = paths_.back();
            path.reserve(path.size() + points.size() + 1U);
        }
        for (const auto& point : points) { add(point); }
    }
private:
    auto start_path(std::size_t capacity) -> clipper2next::Path64& {
        paths_.emplace_back();
        started_ = true;
        auto& path = paths_.back();
        path.reserve(capacity);
        path.emplace_back(first_);
        pending_ = false;
        return path;
    }
    auto finish() noexcept -> void {
        if (started_ && paths_.back().size() < 2U) { paths_.pop_back(); }
        started_ = false;
        pending_ = false;
    }
    clipper2next::Paths64& paths_;
    clipper2next::Point64 first_{};
    bool started_{};
    bool pending_{};
};

[[nodiscard]] auto get_location(const clipper2next::Rect64& rect,
                                const clipper2next::Point64& point,
                                Location& location) -> bool {
    location = clipper2next::internal::classify_point(rect, point);
    return !clipper2next::internal::is_on_rect_boundary(rect, point);
}
}  // namespace

namespace clipper2next::internal {
rectclip_line_executor::rectclip_line_executor(const Rect64& rect) noexcept
    : rect_(&rect) {}

auto rectclip_line_executor::execute_path(Paths64& output, const Path64& path)
    -> void {
    const auto& rect = *rect_;
    if (rect.is_empty() || path.size() < 2) { return; }
    line_output execution{output};

    size_t i = 1;
    const size_t high_i = path.size() - 1;

    Location prev = Location::Inside;
    Location loc;
    Location crossing_loc;
    if (!get_location(rect, path[0], loc)) {
        while (i <= high_i && !get_location(rect, path[i], prev)) { ++i; }
        if (i > high_i) {
            execution.add(std::span<const Point64>{path});
            return;
        }
        if (prev == Location::Inside) { loc = Location::Inside; }
        i = 1;
    }
    if (loc == Location::Inside) { execution.add(path[0]); }

    while (i <= high_i) {
        prev = loc;
        advance_rectclip_location(rect, path, loc, i, high_i,
            [&](std::span<const Point64> points) { execution.add(points); });
        if (i > high_i) { break; }
        Point64 ip;
        Point64 ip2;
        const Point64 prev_pt = path[static_cast<size_t>(i - 1)];

        crossing_loc = loc;
        if (!get_intersection(rect, path[i], prev_pt, crossing_loc, ip)) {
            ++i;
            continue;
        }

        if (loc == Location::Inside) {
            execution.add(ip, true);
        } else if (prev != Location::Inside) {
            crossing_loc = prev;
            if (!get_intersection(rect, prev_pt, path[i], crossing_loc, ip2)) {
                ++i;
                continue;
            }
            execution.add(ip2, true);
            execution.add(ip);
        } else {
            execution.add(ip);
        }
    }
}
}  // namespace clipper2next::internal
