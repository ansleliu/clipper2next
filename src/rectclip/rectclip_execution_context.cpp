#include "rectclip/private/rectclip_execution_context.h"

namespace clipper2next::internal {

auto rectclip_execution_context::add(Point64 point, bool start_new) -> rectclip_node& {
    auto& context = *context_;
    auto current_index = context.results.size();
    rectclip_node* result = nullptr;

    if (current_index == 0 || start_new) {
        result = &context.op_container.emplace();
        result->pt = point;
        result->next = result;
        result->prev = result;
        context.results.emplace_back(result);
    } else {
        --current_index;
        auto* previous = context.results[current_index].get();
        if (previous->pt == point) { return *previous; }

        result = &context.op_container.emplace();
        result->owner_index = current_index;
        result->pt = point;
        result->next = previous->next;
        previous->next->prev = result;
        previous->next = result;
        result->prev = previous;
        context.results[current_index] = result;
    }

    return *result;
}

auto rectclip_execution_context::add_corner(rect_location previous, rect_location current) -> void {
    const auto index = internal::heading_clockwise(previous, current) ? previous : current;
    static_cast<void>(add(context_->rect_as_path[static_cast<size_t>(index)]));
}

auto rectclip_execution_context::add_corner(rect_location& location, bool clockwise) -> void {
    if (clockwise) {
        static_cast<void>(add(context_->rect_as_path[static_cast<size_t>(location)]));
        location = internal::adjacent_location(location, true);
    } else {
        location = internal::adjacent_location(location, false);
        static_cast<void>(add(context_->rect_as_path[static_cast<size_t>(location)]));
    }
}

auto rectclip_execution_context::get_next_location(const Path64& path,
                                                   rect_location& location,
                                                   size_t& index,
                                                   size_t high_index) -> void {
    auto& context = *context_;
    if (location != rect_location::Inside) {
        location =
            internal::next_external_location(context.rect, path, location, index, high_index);
        return;
    }

    while (index <= high_index) {
        if (path[index].x < context.rect.left) {
            location = rect_location::Left;
        } else if (path[index].x > context.rect.right) {
            location = rect_location::Right;
        } else if (path[index].y > context.rect.bottom) {
            location = rect_location::Bottom;
        } else if (path[index].y < context.rect.top) {
            location = rect_location::Top;
        } else {
            static_cast<void>(add(path[index]));
            ++index;
            continue;
        }
        break;
    }
}

}  // namespace clipper2next::internal
