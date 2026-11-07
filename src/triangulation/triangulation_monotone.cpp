#include "triangulation/private/triangulation_monotone.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "geometry/private/geometry_predicates.h"
#include "support/private/storage/release_vector.h"

namespace clipper2next::internal {
namespace {
enum class monotone_axis { vertical, horizontal };
// Horizontal ordering is the orientation-preserving rotation (-y,x),
// while all points and predicates keep their original coordinates.
auto projected_before(Point64 a,Point64 b,monotone_axis axis) noexcept -> bool {
    return axis==monotone_axis::horizontal
        ? (a.x>b.x || (a.x==b.x && a.y>b.y))
        : sweep_before(a,b);
}
auto point(const Path64& path, int direction, std::size_t index) -> Point64 {
    return path[direction > 0 ? index : path.size() - 1 - index];
}
// Each certified chain is a contiguous cyclic range in the original ring.
// Its indices are borrowed facts, not a second owned copy of the path order.
struct monotone_chain final {
    std::size_t first{}, ring_size{}, count{};
    bool forward{};

    auto size() const noexcept -> std::size_t { return count; }
    auto operator[](std::size_t offset) const noexcept -> std::size_t {
        if (forward) {
            const auto remaining = ring_size - first;
            return offset < remaining ? first + offset : offset - remaining;
        }
        return offset <= first ? first - offset : ring_size - (offset - first);
    }
    auto front() const noexcept -> std::size_t { return first; }
    auto back() const noexcept -> std::size_t { return (*this)[count - 1U]; }
};

auto chain(const Path64& path, int direction, std::size_t first, std::size_t last,
           bool forward,monotone_axis axis, monotone_chain& result) -> bool {
    const auto distance = forward
        ? (last >= first ? last - first : path.size() - first + last)
        : (first >= last ? first - last : path.size() - last + first);
    result = {first, path.size(), distance + 1U, forward};
    auto i = first;
    for (std::size_t offset = 1; offset < result.size(); ++offset) {
        const auto next = result[offset];
        if (!projected_before(point(path, direction, i), point(path, direction, next),axis)) { return false; }
        i = next;
    }
    return true;
}
auto separated(const Path64& path, int direction, const monotone_chain& first,
               const monotone_chain& other, bool forward,
                const triangulation_predicate_domain& domain,monotone_axis axis) -> bool {
    std::size_t cursor = 0;
    for (std::size_t k = 1; k + 1 < first.size(); ++k) {
        const auto p = point(path, direction, first[k]);
        while (cursor + 1 < other.size() - 1 &&
               projected_before(point(path, direction, other[cursor + 1]), p,axis)) { ++cursor; }
        const auto a = point(path, direction, other[cursor]);
        const auto b = point(path, direction, other[cursor + 1]);
        if (!projected_before(a, p,axis) || !projected_before(p, b,axis)) { return false; }
        const auto turn = domain.orientation(a, b, p);
        if (forward ? turn >= 0 : turn <= 0) { return false; }
    }
    return true;
}
auto merge(const Path64& path, int direction, const monotone_chain& first,
           const monotone_chain& second,monotone_axis axis,
           std::vector<monotone_vertex>& result) -> void {
    result.clear();
    result.reserve(path.size());
    result.push_back({first.back(), 2});
    auto a = first.size() - 2, b = second.size() - 2;
    while (a || b) {
        if (!b || (a && projected_before(point(path, direction, second[b]),
                                     point(path, direction, first[a]),axis))) {
            result.push_back({first[a--], 0});
        } else {
            result.push_back({second[b--], 1});
        }
    }
    result.push_back({first.front(), 2});
}
}  // namespace

auto monotone_workspace::clear() noexcept -> void {
    order.clear();
    stack.clear();
}
auto monotone_workspace::release() noexcept -> void {
    release_vector(order);
    release_vector(stack);
}

auto prepare_monotone_layout(const Path64& path,
                             const triangulation_predicate_domain& domain,
                             monotone_workspace& workspace) -> std::optional<monotone_layout> {
    workspace.clear();
    if (path.size() < 3) { return std::nullopt; }
    const auto bounds=domain.bounds();
    const auto preferred=bounds.right-bounds.left>bounds.bottom-bounds.top
        ? monotone_axis::horizontal : monotone_axis::vertical;
    const auto other=preferred==monotone_axis::horizontal
        ? monotone_axis::vertical : monotone_axis::horizontal;
    // Both orthogonal projections use one certificate. Preserve every already
    // certifiable vertical domain when the preferred projection is not monotone.
    for(auto axis:{preferred,other}) {
        std::size_t first=0,last=0;
        for(std::size_t i=1;i<path.size();++i) {
            if(projected_before(path[i],path[first],axis))first=i;
            if(projected_before(path[last],path[i],axis))last=i;
        }
        if(first==last)continue;
        // A certified monotone ring has a convex supporting corner here.
        // Its direction is accepted only after both complete chain proofs.
        const auto previous=first?first-1U:path.size()-1U;
        const auto next=first+1U==path.size()?0U:first+1U;
        const auto direction=domain.orientation(path[previous],path[first],path[next]);
        if(!direction)continue;
        if(direction<0){first=path.size()-1U-first;last=path.size()-1U-last;}
        monotone_chain a, b;
        if(!chain(path,direction,first,last,true,axis,a) ||
           !chain(path,direction,first,last,false,axis,b) ||
           !separated(path,direction,a,b,true,domain,axis) ||
           !separated(path,direction,b,a,false,domain,axis))continue;
        merge(path,direction,a,b,axis,workspace.order);
        return monotone_layout{direction,workspace.order};
    }
    return std::nullopt;
}
}  // namespace clipper2next::internal
