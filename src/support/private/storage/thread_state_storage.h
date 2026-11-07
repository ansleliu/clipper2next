#pragma once

namespace clipper2next::internal {

// The registry is a constant-initialized POD pointer: peeking neither constructs
// a State nor registers a class TLS destructor. Publish only completed owners.
template <class State>
class thread_state_storage final {
    inline static constinit thread_local State* existing_ = nullptr;
    struct owner final {
        State state{};
        owner() { existing_ = &state; }
        ~owner() { existing_ = nullptr; }
    };

public:
    [[nodiscard]] static auto acquire() -> State& {
        thread_local owner storage;
        return storage.state;
    }
    [[nodiscard]] static auto peek_existing_slot() noexcept -> State* { return existing_; }
};
}  // namespace clipper2next::internal
