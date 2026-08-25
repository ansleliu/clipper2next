#include "clipper2next/offset/builder.h"

#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"

#include <utility>
#include <vector>

namespace clipper2next {

namespace {

}  // namespace

struct offset_builder::impl final {
    std::vector<internal::offset_group> groups{};
    double delta{0.0};
    JoinType join_type{JoinType::Miter};
    EndType end_type{EndType::Polygon};
    double miter_limit{2.0};
    double arc_tolerance{0.0};
    execution_options options{};
    DeltaCallback64 delta_callback{};

    impl() = default;

    impl(const impl& other)
        : groups(other.groups),
          delta(other.delta),
          join_type(other.join_type),
          end_type(other.end_type),
          miter_limit(other.miter_limit),
          arc_tolerance(other.arc_tolerance),
          options(other.options),
          delta_callback(other.delta_callback) {}

    auto operator=(const impl& other) -> impl& {
        if (this != &other) {
            groups = other.groups;
            delta = other.delta;
            join_type = other.join_type;
            end_type = other.end_type;
            miter_limit = other.miter_limit;
            arc_tolerance = other.arc_tolerance;
            options = other.options;
            delta_callback = other.delta_callback;
        }
        return *this;
    }

    auto execute(Paths64& solution, PolyTree64* solution_tree) const -> void {
        internal::offset_state state;
        internal::execute_offset_algorithm(
            state,
            groups,
            delta,
            solution,
            solution_tree,
            internal::offset_algorithm_options{
                .miter_limit = miter_limit,
                .arc_tolerance = arc_tolerance,
                .preserve_collinear = options.preserve_collinear,
                .reverse_solution = options.reverse_solution,
            },
            delta_callback_ref{delta_callback});
    }
};

offset_builder::offset_builder()
    : impl_(std::make_unique<impl>()) {}
offset_builder::offset_builder(const offset_builder& other)
    : impl_(std::make_unique<impl>(*other.impl_)) {}
auto offset_builder::operator=(const offset_builder& other) -> offset_builder& {
    if (this != &other) { impl_ = std::make_unique<impl>(*other.impl_); }
    return *this;
}
offset_builder::offset_builder(offset_builder&&) noexcept = default;
auto offset_builder::operator=(offset_builder&&) noexcept -> offset_builder& = default;
offset_builder::~offset_builder() = default;

auto offset_builder::delta(double value) -> offset_builder& {
    impl_->delta = value;
    return *this;
}

auto offset_builder::join(JoinType value) -> offset_builder& {
    impl_->join_type = value;
    return *this;
}

auto offset_builder::end(EndType value) -> offset_builder& {
    impl_->end_type = value;
    return *this;
}

auto offset_builder::miter_limit(double value) -> offset_builder& {
    impl_->miter_limit = value;
    return *this;
}

auto offset_builder::arc_tolerance(double value) -> offset_builder& {
    impl_->arc_tolerance = value;
    return *this;
}

// cppcheck-suppress passedByValue
auto offset_builder::options(execution_options value) -> offset_builder& {
    impl_->options = value;
    return *this;
}

auto offset_builder::preserve_collinear(bool value) -> offset_builder& {
    impl_->options.preserve_collinear = value;
    return *this;
}

auto offset_builder::reverse_solution(bool value) -> offset_builder& {
    impl_->options.reverse_solution = value;
    return *this;
}

auto offset_builder::delta_callback(DeltaCallback64 callback) -> offset_builder& {
    impl_->delta_callback = std::move(callback);
    return *this;
}

auto offset_builder::add(const Path64& path) -> offset_builder& {
    impl_->groups.emplace_back(Paths64{path}, impl_->join_type, impl_->end_type);
    return *this;
}

auto offset_builder::add(const Paths64& paths) -> offset_builder& {
    if (!paths.empty()) { impl_->groups.emplace_back(paths, impl_->join_type, impl_->end_type); }
    return *this;
}

auto offset_builder::clear() -> offset_builder& {
    impl_->groups.clear();
    return *this;
}

auto offset_builder::execute() const -> Paths64 {
    Paths64 solution;
    execute_into(solution);
    return solution;
}

auto offset_builder::execute_into(Paths64& solution) const -> void {
    solution.clear();
    impl_->execute(solution, nullptr);
}

auto offset_builder::execute_into(PolyTree64& solution) const -> void {
    solution.clear();
    Paths64 scratch;
    impl_->execute(scratch, &solution);
}

}  // namespace clipper2next
