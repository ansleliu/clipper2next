#pragma once

#include "clipper2next/core.h"
#include <algorithm>

namespace clipper2next::internal {

// Capacity preparation may append empty slots but must not publish them on
// failure. Keep live paths until begin so both coordinates and cardinality survive.
template <typename Coordinate>
class paths_rebuild final {
public:
    explicit paths_rebuild(Paths<Coordinate>& paths) noexcept
        : paths_(paths),
          original_size_(paths.size()) {}
    paths_rebuild(const paths_rebuild&) = delete;
    ~paths_rebuild() {
        if (!committed_) { paths_.resize(begun_ ? 0U : original_size_); }
    }
    auto prepare_slots(std::size_t count) -> void {
        count_ = count;
        paths_.resize((std::max)(original_size_, count));
    }
    template <typename PointCount>
    auto prepare(std::size_t count, PointCount point_count) -> void {
        prepare_slots(count);
        for (std::size_t i = 0; i < count; ++i) { paths_[i].reserve(point_count(i)); }
    }
    auto begin() noexcept -> void {
        begun_ = true;
        for (auto& path : paths_) { path.clear(); }
    }
    auto finish() noexcept -> void {
        paths_.resize(count_);
        committed_ = true;
    }

private:
    Paths<Coordinate>& paths_;
    std::size_t original_size_;
    std::size_t count_{};
    bool begun_{};
    bool committed_{};
};

// Source must remain valid during preparation and cannot alias destination.
// Algorithm materializers obtain that lifetime from their kernel or cache owner.
template <typename Coordinate>
auto materialize_paths(const Paths<Coordinate>& source, Paths<Coordinate>& destination) -> void {
    if (destination.capacity() == 0U) {
        // A fresh output has no retained buffers. Construct its points directly
        // and publish only after the complete immutable copy succeeds.
        auto completed = source;
        destination.swap(completed);
        return;
    }
    paths_rebuild<Coordinate> transaction{destination};
    transaction.prepare(source.size(), [&](auto i) { return source[i].size(); });
    transaction.begin();
    for (std::size_t i = 0; i < source.size(); ++i) {
        destination[i].insert(destination[i].end(), source[i].begin(), source[i].end());
    }
    transaction.finish();
}


// Exclusive completed buffers can be published without another point allocation.
// Source and destination must be distinct; immutable/borrowed sources use the const overload.
template <typename Coordinate>
auto materialize_paths(Paths<Coordinate>&& source, Paths<Coordinate>& destination) -> void {
    if (destination.empty() && source.capacity() == source.size() &&
        source.capacity() >= destination.capacity()) {
        destination.swap(source);
        return;
    }
    paths_rebuild<Coordinate> transaction{destination};
    transaction.prepare_slots(source.size());
    transaction.begin();
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (destination[i].capacity() < source[i].size()) {
            destination[i].swap(source[i]);
        } else {
            destination[i].insert(destination[i].end(), source[i].begin(), source[i].end());
        }
    }
    transaction.finish();
}

}  // namespace clipper2next::internal
