// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_MPSCQ_H_
#define CXXIME_MPSCQ_H_

#include <stdint.h>
#include <atomic>

namespace cxxime {

// Atomic wrapper with enum-based memory order (avoids raw casts at call sites).
template <typename T>
class Atomic {
public:
    explicit Atomic(T val = T()) : value_(val) {}

    T Load(std::memory_order order = std::memory_order_relaxed) const {
        return value_.load(order);
    }

    void Store(T val, std::memory_order order = std::memory_order_relaxed) {
        value_.store(val, order);
    }

    T Exchange(T desired, std::memory_order order) {
        return value_.exchange(desired, order);
    }

    bool CompareExchangeWeak(T* expected, T desired,
                             std::memory_order success, std::memory_order failure) {
        return value_.compare_exchange_weak(*expected, desired, success, failure);
    }

    bool CompareExchangeStrong(T* expected, T desired,
                               std::memory_order success, std::memory_order failure) {
        return value_.compare_exchange_strong(*expected, desired, success, failure);
    }

    T FetchAdd(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_add(arg, order);
    }

    T FetchSub(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_sub(arg, order);
    }

private:
    std::atomic<T> value_;
};

// Lock-free multi-producer single-consumer queue.
// Producers call push() concurrently; a single consumer calls pop().
// Nodes are caller-owned — the queue only links them.
class MPSCQueue final {
public:
    struct Node {
        Atomic<Node*> next{nullptr};
    };

    MPSCQueue();
    ~MPSCQueue();

    // Push a node into the queue. Returns true if this was the first element.
    // Thread-safe for multiple concurrent producers.
    bool push(Node* node);

    // Pop the oldest node. Returns nullptr if queue is empty.
    // NOT thread-safe — must be called by a single consumer.
    Node* pop();

    // Pop with end-of-queue detection.
    Node* pop_and_check_end(bool* empty);

private:
    union {
        char padding[64];
        Atomic<Node*> newest_;
    };
    Node* oldest_;
    Node stub_;
};

} // namespace cxxime

#endif // CXXIME_MPSCQ_H_
