// include/incremental/detail/callback_queue.hpp
// Queue for mutations that arrive during the notification phase.
// Implements FIFO ordering and enforces a safety depth limit.
#pragma once

#include <incremental/detail/type_erasure.hpp>
#include <functional>
#include <queue>
#include <stdexcept>

namespace incremental::detail {

// A deferred mutation request: a callable that will trigger a transaction.
using DeferredMutation = std::function<void()>;

// ── CallbackQueue ─────────────────────────────────────────────────────────────
// Stores mutations requested by observers during the notification phase.
// After the notification phase completes, the graph drains these queued
// mutations one at a time as independent transactions.
//
// Safety limit: if draining itself triggers callbacks that re-enqueue, we
// count depth. Exceeding 'max_depth' throws ReentrancyError.

class CallbackQueue {
public:
    explicit CallbackQueue(int max_depth = 32) : max_depth_(max_depth) {}

    void enqueue(DeferredMutation mut) {
        pending_.push(std::move(mut));
    }

    [[nodiscard]] bool has_pending() const noexcept {
        return !pending_.empty();
    }

    // Pop and return the next mutation. Returns nullptr if empty.
    [[nodiscard]] DeferredMutation pop() {
        if (pending_.empty()) return nullptr;
        auto m = std::move(pending_.front());
        pending_.pop();
        return m;
    }

    void increment_drain_depth() {
        if (++drain_depth_ > max_depth_) {
            throw std::runtime_error(
                std::format(
                    "Callback feedback loop exceeded safety limit of {} "
                    "consecutive queued transactions. Check observer logic.",
                    max_depth_));
        }
    }

    void reset_drain_depth() noexcept { drain_depth_ = 0; }

    [[nodiscard]] int drain_depth() const noexcept { return drain_depth_; }
    [[nodiscard]] int max_depth() const noexcept { return max_depth_; }

    void clear() {
        while (!pending_.empty()) pending_.pop();
        drain_depth_ = 0;
    }

private:
    std::queue<DeferredMutation> pending_;
    int drain_depth_{0};
    int max_depth_;
};

} // namespace incremental::detail
