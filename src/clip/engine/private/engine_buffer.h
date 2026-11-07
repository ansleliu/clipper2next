#pragma once

#include "support/private/execution_resource_ledger.h"
#include "support/private/checked_size.h"
#include "support/private/storage/release_vector.h"
#include <span>
#include <vector>
#include <iterator>
#include <type_traits>

namespace clipper2next::internal {

// Effective elements and their fixed workspace category have one owner.
// Capacity is reusable cache and never enters the operation ledger.
template<class T, workspace_category Category = workspace_category::engine>
class engine_buffer final {
public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    engine_buffer() = default;
    explicit engine_buffer(execution_resource_ledger* context) noexcept
        : charge_(context, Category) {}
    engine_buffer(std::initializer_list<T> values) : values_(values) {}
    engine_buffer(const engine_buffer& other) : charge_(other.charge_.context(), Category) {
        assign(other.begin(), other.end());
    }
    engine_buffer(engine_buffer&&) noexcept = default;
    auto operator=(engine_buffer&&) -> engine_buffer& = delete;
    auto operator=(const engine_buffer&) -> engine_buffer& = delete;
    auto bind(execution_resource_ledger* context) -> void {
        charge_ = scoped_workspace_charge{context, Category};
        if (const auto error = charge_.try_set_slots(size(), sizeof(T)); error != clipper_error_code::ok) {
            detach();
            raise_clipper_error(error);
        }
    }
    auto detach() noexcept -> void { charge_ = scoped_workspace_charge{nullptr, Category}; }
    auto begin() noexcept { return values_.begin(); }
    auto end() noexcept { return values_.end(); }
    auto begin() const noexcept { return values_.begin(); }
    auto end() const noexcept { return values_.end(); }
    auto cbegin() const noexcept { return values_.cbegin(); }
    auto cend() const noexcept { return values_.cend(); }
    auto rbegin() noexcept { return values_.rbegin(); }
    auto rend() noexcept { return values_.rend(); }
    auto size() const noexcept { return values_.size(); }
    auto capacity() const noexcept { return values_.capacity(); }
    auto empty() const noexcept { return values_.empty(); }
    auto data() noexcept { return values_.data(); }
    auto data() const noexcept { return values_.data(); }
    auto operator[](std::size_t i) noexcept -> T& { return values_[i]; }
    auto operator[](std::size_t i) const noexcept -> const T& { return values_[i]; }
    auto back() noexcept -> T& { return values_.back(); }
    auto back() const noexcept -> const T& { return values_.back(); }
    auto front() noexcept -> T& { return values_.front(); }
    auto front() const noexcept -> const T& { return values_.front(); }
    auto reserve(std::size_t n) -> void { values_.reserve(n); }
    auto resize(std::size_t n) -> void { change_size(n, [&] { values_.resize(n); }); }
    template<class... Args> auto emplace_back(Args&&... args) -> T& {
        static_cast<void>(checked_size_add(size(), 1U));
        if (!charge_.context()) { return values_.emplace_back(std::forward<Args>(args)...); }
        const auto next = checked_size_add(charge_.bytes(), sizeof(T));
        if constexpr (std::is_trivially_constructible_v<T, Args...> &&
                      std::is_nothrow_constructible_v<T, Args...>) {
            if (size() < capacity()) {
                T* result{};
                const auto error = charge_.try_construct_bytes<Category>(next, [&]() noexcept {
                    result = &values_.emplace_back(std::forward<Args>(args)...);
                });
                if (error != clipper_error_code::ok) { raise_clipper_error(error); }
                return *result;
            }
        }
        return change_bytes(next, [&]() -> T& {
            return values_.emplace_back(std::forward<Args>(args)...);
        });
    }
    // Stable, distinct contiguous source; trivial copies cannot call user code.
    // Preserve sequential append's failure prefix when a whole run cannot fit.
    auto append_range(std::span<const T> source) -> void {
        static_assert(std::is_trivially_copy_constructible_v<T> &&
                      std::is_trivially_copy_assignable_v<T>);
        if (source.empty()) { return; }
        if (source.size() <= capacity() - size()) {
            if (!charge_.context()) {
                values_.insert(values_.end(), source.begin(), source.end());
                return;
            }
            const auto error = charge_.try_construct_bytes<Category>(
                checked_size_multiply(size() + source.size(), sizeof(T)), [&]() noexcept {
                    values_.insert(values_.end(), source.begin(), source.end());
                });
            if (error == clipper_error_code::ok) { return; }
        }
        for (const auto& value : source) { emplace_back(value); }
    }
    // Stable no-throw generation must not allocate, reenter or change other owners.
    // Use existing capacity; rejected bulk admission preserves the sequential prefix.
    template<class Generate> auto append_generated(std::size_t count, Generate&& generate) -> void {
        static_assert(std::is_trivially_default_constructible_v<T> &&
                      std::is_nothrow_default_constructible_v<T> &&
                      std::is_trivially_copy_constructible_v<T> &&
                      std::is_trivially_copy_assignable_v<T> &&
                      std::is_nothrow_invocable_r_v<T, Generate&>);
        if (count == 0U) { return; }
        const auto first = size();
        const auto fill = [&]() noexcept {
            for (auto index = std::size_t{}; index < count; ++index) {
                values_[first + index] = generate();
            }
        };
        if (count <= capacity() - size()) {
            if (!charge_.context()) {
                values_.resize(first + count);
                fill();
                return;
            }
            const auto error = charge_.try_construct_bytes<Category>(
                checked_size_multiply(first + count, sizeof(T)), [&]() noexcept {
                    values_.resize(first + count);
                });
            if (error == clipper_error_code::ok) {
                fill();
                return;
            }
        }
        for (auto index = std::size_t{}; index < count; ++index) {
            change_size(checked_size_add(size(), 1U), [&] { values_.emplace_back(generate()); });
        }
    }
    auto push_back(const T& value) -> void { emplace_back(value); }
    auto push_back(T&& value) -> void { emplace_back(std::move(value)); }
    auto pop_back() noexcept -> void {
        values_.pop_back();
        if (charge_.context()) { charge_.release_bytes(sizeof(T)); }
    }
    auto clear() noexcept -> void { values_.clear(); charge_.release(); }
    auto release() noexcept -> void { clear(); release_vector(values_); }
    template<class It> auto assign(It first, It last) -> void {
        const auto n = static_cast<std::size_t>(std::distance(first, last));
        change_size(n, [&] { values_.assign(first, last); });
    }
    template<class It> auto insert(const_iterator where, It first, It last) -> iterator {
        return change_size(checked_size_add(size(), static_cast<std::size_t>(std::distance(first, last))), [&] {
            return values_.insert(where, first, last);
        });
    }
    auto erase(const_iterator first, const_iterator last) -> iterator {
        const auto removed = static_cast<std::size_t>(last - first);
        auto result = values_.erase(first, last);
        if (charge_.context()) { charge_.release_bytes(removed * sizeof(T)); }
        return result;
    }
    auto erase(const_iterator where) -> iterator { return erase(where, std::next(where)); }
    auto swap(engine_buffer& other) noexcept -> void {
        values_.swap(other.values_); std::swap(charge_, other.charge_);
    }
private:
    template<class Function> auto change_size(std::size_t next, Function&& function) -> decltype(auto) {
        if (!charge_.context()) { return function(); }
        return change_bytes(checked_size_multiply(next, sizeof(T)), std::forward<Function>(function));
    }
    template<class Function> auto change_bytes(std::size_t next, Function&& function) -> decltype(auto) {
        auto growth = scoped_workspace_growth{charge_, next, 1U};
        if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
            function();
            growth.commit();
        } else {
            decltype(auto) result = function();
            growth.commit();
            return result;
        }
    }
    scoped_workspace_charge charge_{nullptr, Category};
    std::vector<T> values_;
};
template<class T>
using staging_buffer = engine_buffer<T, workspace_category::staging>;

template<class T, workspace_category Category>
auto release_vector(engine_buffer<T, Category>& values) noexcept -> void { values.release(); }

} // namespace clipper2next::internal
