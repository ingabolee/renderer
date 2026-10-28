// include/incremental/observer.hpp
// Observer type for receiving commit notifications.
#pragma once

#include <incremental/change_event.hpp>
#include <functional>
#include <cstdint>

namespace incremental {

// ── ObserverHandle ────────────────────────────────────────────────────────────
// Opaque token identifying a registered observer. Used to unsubscribe.

using ObserverId = std::uint64_t;
static constexpr ObserverId kInvalidObserverId = 0;

struct ObserverHandle {
    ObserverId id{kInvalidObserverId};
    [[nodiscard]] bool valid() const noexcept { return id != kInvalidObserverId; }
    explicit operator bool() const noexcept { return valid(); }
};

// ── ObserverFn ────────────────────────────────────────────────────────────────
// Callback signature: receives a const ChangeEvent& after each commit.
//
// CONTRACT:
// - The callback is invoked only after the new consistent state is published.
// - The observer MAY read any committed node value safely.
// - The observer MAY call graph.transaction() and stage mutations. Such a
//   request is queued and processed as a separate transaction after the current
//   notification batch completes (it returns CommitResult with queued=true).
// - The observer MUST NOT assume that the in-progress notification is the only
//   change event that will be delivered; queued mutations yield additional events.
// - If the observer throws, the exception is caught, logged (if a handler is set),
//   and the next observer is still called. The committed state is NOT rolled back.

using ObserverFn = std::function<void(const ChangeEvent&)>;

} // namespace incremental
