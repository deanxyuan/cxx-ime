// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/mpscq.h>
#include <assert.h>

namespace cxxime {

MPSCQueue::MPSCQueue()
    : newest_{&stub_}
    , oldest_(&stub_) {}

MPSCQueue::~MPSCQueue() {
    assert(newest_.Load() == &stub_);
    assert(oldest_ == &stub_);
}

bool MPSCQueue::push(Node* node) {
    node->next.Store(nullptr, std::memory_order_relaxed);
    Node* prev = newest_.Exchange(node, std::memory_order_acq_rel);
    prev->next.Store(node, std::memory_order_release);
    return prev == &stub_;
}

MPSCQueue::Node* MPSCQueue::pop() {
    bool empty = false;
    return pop_and_check_end(&empty);
}

MPSCQueue::Node* MPSCQueue::pop_and_check_end(bool* empty) {
    Node* tail = oldest_;
    Node* obj = oldest_->next.Load(std::memory_order_acquire);
    if (tail == &stub_) {
        if (obj == nullptr) {
            *empty = true;
            return nullptr;
        }
        oldest_ = obj;
        tail = obj;
        obj = tail->next.Load(std::memory_order_acquire);
    }

    if (obj != nullptr) {
        *empty = false;
        oldest_ = obj;
        return tail;
    }

    Node* head = newest_.Load(std::memory_order_acquire);
    if (tail != head) {
        *empty = false;
        return nullptr;
    }

    push(&stub_);
    obj = tail->next.Load(std::memory_order_acquire);
    if (obj != nullptr) {
        *empty = false;
        oldest_ = obj;
        return tail;
    }

    *empty = false;
    return nullptr;
}

} // namespace cxxime
